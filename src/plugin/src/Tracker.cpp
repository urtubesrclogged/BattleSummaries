#include "Tracker.h"

#include "Core.h"
#include "Effects.h"
#include "Narrative.h"
#include "Perf.h"
#include "Settings.h"
#include "SkyrimNet.h"

#include <nlohmann/json.hpp>

namespace BSM::Tracker
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		double Now()
		{
			static const auto start = Clock::now();
			return std::chrono::duration<double>(Clock::now() - start).count();
		}

		// ---- what the game reports, queued where it happens (any thread) ----
		struct Report
		{
			enum class Type
			{
				kCombat,    // a entered combat with b
				kDamage,    // a took amount from b; pct = a's health share afterwards
				kDeath,     // a died, killed by b
				kBleedout,  // a went down
				kEffect     // magic effect `effect` from b took hold on a
			};
			Type       type;
			ActorId    a{ 0 };
			ActorId    b{ 0 };
			float      amount{ 0.0f };
			float      pct{ -1.0f };
			RE::FormID effect{ 0 };
			std::uintptr_t source{ 0 };  // kDamage: the hook's second argument as it came, never dereferenced there
			double     at{ 0.0 };
		};

		std::mutex          g_queueLock;  // held only to push or swap; never across a game call
		std::vector<Report> g_queue;
		std::atomic<bool>   g_installed{ false };

		void Push(Report a_r)
		{
			a_r.at = Now();
			std::scoped_lock l{ g_queueLock };
			if (g_queue.size() < 4096) g_queue.push_back(a_r);
			else Perf::queueDropped.fetch_add(1, std::memory_order_relaxed);
		}

		// ---- the model (main thread writes; the Papyrus thread reads a summary) ----
		std::mutex            g_lock;
		std::optional<Battle> g_current;
		std::deque<Battle>    g_history;  // newest first

		struct Extra  // what the tracker knows about a battle beyond the model
		{
			std::set<ActorId> witnesses;
			bool              remembered{ false };
		};
		std::map<int, Extra> g_extra;

		struct Live
		{
			RE::ActorHandle handle;
			float           lastHealth{ -1.0f };
			float           damageSince{ 0.0f };
			float           pendingLoss{ 0.0f };  // health lost last tick that no damage report explained (yet)
		};
		std::unordered_map<ActorId, Live> g_live;

		struct RecentHeal
		{
			ActorId     target, healer;
			std::string source;
			double      at;
		};
		std::vector<RecentHeal> g_recentHeals;

		double g_quiet{ 0.0 };
		double g_lastTick{ 0.0 };
		int    g_nextId{ 1 };
		std::string g_lastMemory;

		constexpr std::size_t kHistory = 3;

		// ---- reading the game (main thread) ----
		float HealthOf(RE::Actor* a_actor) { return a_actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth); }

		float MaxHealthOf(RE::Actor* a_actor)
		{
			const float max = a_actor->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kHealth) +
							  a_actor->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kHealth);
			return max > 1.0f ? max : 1.0f;
		}

		float HealthPct(RE::Actor* a_actor) { return std::clamp(HealthOf(a_actor) / MaxHealthOf(a_actor), 0.0f, 1.0f); }

		bool OnPlayerSide(RE::Actor* a_actor, RE::Actor* a_pc, ActorId* a_master = nullptr)
		{
			if (a_actor == a_pc || a_actor->IsPlayerTeammate()) return true;
			if (a_actor->IsCommandedActor()) {
				if (const auto boss = a_actor->GetCommandingActor()) {
					if (a_master) *a_master = boss->GetFormID();
					return boss.get() == a_pc || boss->IsPlayerTeammate();
				}
			}
			return false;
		}

		ActorInfo InfoOf(RE::Actor* a_actor)
		{
			auto*     pc = RE::PlayerCharacter::GetSingleton();
			ActorInfo i;
			i.id = a_actor->GetFormID();
			const char* n = a_actor->GetDisplayFullName();
			i.name = n ? n : "";
			while (!i.name.empty() && std::isspace(static_cast<unsigned char>(i.name.back()))) i.name.pop_back();  // name mods leave a trailing space
			if (i.name.empty()) i.name = "someone";
			i.isPlayer = a_actor == pc;
			i.maxHealth = MaxHealthOf(a_actor);
			if (OnPlayerSide(a_actor, pc, &i.master)) i.side = Side::kPlayer;
			else if (a_actor->IsHostileToActor(pc)) i.side = Side::kEnemy;
			else i.side = Side::kOther;
			return i;
		}

		RE::Actor* Lookup(ActorId a_id) { return a_id ? RE::TESForm::LookupByID<RE::Actor>(a_id) : nullptr; }

		bool Near(RE::Actor* a_actor)
		{
			auto* pc = RE::PlayerCharacter::GetSingleton();
			if (!a_actor || !pc) return false;
			if (a_actor == pc) return true;
			return a_actor->Is3DLoaded() && a_actor->GetPosition().GetDistance(pc->GetPosition()) <= Settings::Get().range;
		}

		std::string WhereNow()
		{
			auto* pc = RE::PlayerCharacter::GetSingleton();
			if (!pc) return {};
			if (auto* loc = pc->GetCurrentLocation()) {
				if (const char* n = loc->GetFullName(); n && *n) return n;
			}
			if (auto* cell = pc->GetParentCell()) {
				if (const char* n = cell->GetFullName(); n && *n) return n;
			}
			// open country has no named place, and the worldspace's own name ("Skyrim") says nothing; a smaller one
			// (Solstheim, a walled city) does
			if (auto* ws = pc->GetWorldspace()) {
				if (const char* n = ws->GetFullName(); n && *n) return std::string_view(n) == "Skyrim" ? "the wilds of Skyrim" : n;
			}
			return {};
		}

		float GameHours()
		{
			auto* cal = RE::Calendar::GetSingleton();
			return cal ? cal->GetHoursPassed() : 0.0f;
		}

		// the caster and carrier of an effect currently on a_target, newest match first
		struct OnTarget
		{
			ActorId     caster{ 0 };
			std::string source;
			const RE::MagicItem* item{ nullptr };
			bool        found{ false };
		};

		class EffectFinder final : public RE::MagicTarget::ForEachActiveEffectVisitor
		{
		public:
			explicit EffectFinder(std::function<void(RE::ActiveEffect*)> a_fn) :
				fn(std::move(a_fn)) {}
			RE::BSContainer::ForEachResult Accept(RE::ActiveEffect* a_effect) override
			{
				if (a_effect) fn(a_effect);
				return RE::BSContainer::ForEachResult::kContinue;
			}
			std::function<void(RE::ActiveEffect*)> fn;
		};

		void ForEachEffect(RE::Actor* a_actor, std::function<void(RE::ActiveEffect*)> a_fn)
		{
			auto* mt = a_actor->AsMagicTarget();
			if (!mt) return;
			EffectFinder v{ std::move(a_fn) };
			mt->VisitEffects(v);
		}

		ActorId CasterOf(RE::ActiveEffect* a_ae)
		{
			const auto c = a_ae->caster.get();
			return c ? c->GetFormID() : 0;
		}

		std::string SourceName(const RE::MagicItem* a_item, const RE::EffectSetting* a_effect)
		{
			if (a_item) {
				if (const char* n = a_item->GetFullName(); n && *n) return n;
			}
			if (a_effect) {
				if (const char* n = a_effect->GetFullName(); n && *n) return n;
			}
			return {};
		}

		OnTarget FindOnTarget(RE::Actor* a_target, const RE::EffectSetting* a_effect, ActorId a_caster)
		{
			OnTarget out;
			float    youngest = 1e9f;
			ForEachEffect(a_target, [&](RE::ActiveEffect* ae) {
				if (ae->GetBaseObject() != a_effect) return;
				const auto caster = CasterOf(ae);
				if (a_caster != 0 && caster != 0 && caster != a_caster) return;
				if (ae->elapsedSeconds >= youngest) return;
				youngest = ae->elapsedSeconds;
				out.caster = caster;
				out.item = ae->spell;
				out.found = true;
			});
			out.source = SourceName(out.item, a_effect);
			return out;
		}

		// ---- the battle's lifecycle (caller holds g_lock, main thread) ----
		void Begin(double a_now)
		{
			const auto& cfg = Settings::Get();
			if (!g_history.empty() && a_now - g_history.front().endedAt <= cfg.mergeGapSeconds) {
				g_current = std::move(g_history.front());  // the fight flared up again: same battle
				g_history.pop_front();
				g_current->endedAt = -1.0;
				SKSE::log::info("Battle {} resumed", g_current->id);
			} else {
				g_current.emplace();
				g_current->id = g_nextId++;
				g_current->startedAt = a_now;
				g_current->location = WhereNow();
				g_current->nearDeathPct = cfg.nearDeathPct;
				g_live.clear();
				g_recentHeals.clear();
				SKSE::log::info("Battle {} began at '{}'", g_current->id, g_current->location);
			}
			g_quiet = 0.0;
		}

		Participant* Join(RE::Actor* a_actor)
		{
			if (!g_current || !a_actor) return nullptr;
			const bool known = g_current->Find(a_actor->GetFormID()) != nullptr;
			auto&      p = g_current->Join(InfoOf(a_actor));
			auto&      live = g_live[p.info.id];
			if (!known) {
				live.handle = a_actor->CreateRefHandle();
				live.lastHealth = HealthOf(a_actor);
				live.damageSince = 0.0f;
				live.pendingLoss = 0.0f;
				SKSE::log::debug("Battle {}: {} ({:08X}) joined, side {}", g_current->id, p.info.name, p.info.id, static_cast<int>(p.info.side));
			}
			return &p;
		}

		void End(double a_foughtUntil)
		{
			if (!g_current) return;
			auto& b = *g_current;
			// an alarm where nothing happened (combat state with no blow struck and nobody dead) is not a battle
			const bool blows = std::ranges::any_of(b.participants, [](const Participant& p) { return p.damageTaken > 0.0f || p.dead; });
			if (b.participants.size() < 2 || !blows || b.Count(Side::kEnemy) == 0) {
				SKSE::log::info("Battle {} came to nothing ({} participants); forgotten", b.id, b.participants.size());
				g_current.reset();
				g_quiet = 0.0;
				return;
			}
			b.endedAt = std::max(a_foughtUntil, b.startedAt);
			b.endedGameHours = GameHours();
			for (auto& p : b.participants) p.isDown = false;

			auto& extra = g_extra[b.id];
			extra.witnesses.clear();
			if (auto* pl = RE::ProcessLists::GetSingleton()) {
				auto* pc = RE::PlayerCharacter::GetSingleton();
				for (auto& h : pl->highActorHandles) {
					const auto a = h.get();
					if (a && pc && !a->IsDead() && a->GetPosition().GetDistance(pc->GetPosition()) <= Settings::Get().witnessRange) {
						extra.witnesses.insert(a->GetFormID());
					}
				}
			}
			NarrativeOptions opt{ Settings::Get().showNumbers, Settings::Get().maxOthers, Settings::Get().maxEffects };
			g_lastMemory = BuildMemory(b, opt);
			SKSE::log::info("Battle {} ended after {:.0f}s, {} participants, {} witnesses: {}", b.id, b.endedAt - b.startedAt, b.participants.size(),
				extra.witnesses.size(), g_lastMemory);

			g_history.push_front(std::move(b));
			g_current.reset();
			while (g_history.size() > kHistory) {
				g_extra.erase(g_history.back().id);
				g_history.pop_back();
			}
			g_quiet = 0.0;
		}

		// ---- working through the reports (caller holds g_lock, main thread) ----
		bool InBattle(RE::Actor* a_actor) { return g_current && a_actor && g_current->Find(a_actor->GetFormID()) != nullptr; }

		// A newcomer belongs to the battle when they are on the player's side, or the one they are fighting is on the
		// player's side or already in the battle as its enemy. Bystanders' quarrels with each other (a hunter and his deer,
		// a wolf and a mudcrab) stay out of it.
		bool Belongs(RE::Actor* a_new, RE::Actor* a_other, RE::Actor* a_pc)
		{
			if (!a_new || !Near(a_new)) return false;
			if (OnPlayerSide(a_new, a_pc)) return true;
			if (!a_other || a_other == a_new) return false;
			if (OnPlayerSide(a_other, a_pc)) return true;
			if (!g_current) return false;
			const auto* p = g_current->Find(a_other->GetFormID());
			return p && p->info.side != Side::kOther;
		}

		// refreshes those already in, admits those who belong
		void JoinPair(RE::Actor* a_a, RE::Actor* a_b, RE::Actor* a_pc)
		{
			const bool a = a_a && (InBattle(a_a) || Belongs(a_a, a_b, a_pc));
			const bool b = a_b && (InBattle(a_b) || Belongs(a_b, a_a, a_pc));
			if (a) Join(a_a);
			if (b) Join(a_b);
		}

		bool PlayerSideInvolved(RE::Actor* a_a, RE::Actor* a_b, RE::Actor* a_pc)
		{
			return (a_a && Near(a_a) && OnPlayerSide(a_a, a_pc)) || (a_b && Near(a_b) && OnPlayerSide(a_b, a_pc));
		}

		// The actor a damage report's raw source value points at, found by comparing it with the actors that exist: the
		// value itself is never read through. nullptr when it is no actor we know (a furniture reference, a number).
		// The actors are listed once per batch of reports (Drain clears the list), not once per report.
		std::unordered_map<std::uintptr_t, RE::Actor*> g_known;
		bool                                           g_knownListed{ false };

		RE::Actor* ResolveSource(std::uintptr_t a_source, RE::Actor* a_pc)
		{
			if (a_source == 0) return nullptr;
			if (!g_knownListed) {
				g_knownListed = true;
				g_known.clear();
				g_known.emplace(reinterpret_cast<std::uintptr_t>(a_pc), a_pc);
				if (auto* pl = RE::ProcessLists::GetSingleton()) {
					for (auto& h : pl->highActorHandles) {
						if (const auto ref = h.get()) g_known.emplace(reinterpret_cast<std::uintptr_t>(ref.get()), ref.get());
					}
				}
				for (auto& [id, live] : g_live) {
					if (const auto ref = live.handle.get()) g_known.emplace(reinterpret_cast<std::uintptr_t>(ref.get()), ref.get());
				}
			}
			const auto it = g_known.find(a_source);
			return it != g_known.end() ? it->second : nullptr;
		}

		void Handle(const Report& a_r)
		{
			auto* pc = RE::PlayerCharacter::GetSingleton();
			auto* a = Lookup(a_r.a);
			auto* b = Lookup(a_r.b);
			if (!pc || !a) return;
			ActorId attacker = a_r.b;
			if (a_r.type == Report::Type::kDamage) {
				b = ResolveSource(a_r.source, pc);
				// a source that is no actor: for the player this is the furniture check, not a blow at all; for anyone else
				// it is damage from something that is not an actor, so nobody's blow
				if (!b && a_r.source != 0 && a == pc) return;
				attacker = b ? b->GetFormID() : 0;
			}

			switch (a_r.type) {
			case Report::Type::kCombat:
				if (!g_current) {
					if (!PlayerSideInvolved(a, b, pc)) return;  // a battle is something the player's side is in
					Begin(a_r.at);
				}
				JoinPair(a, b, pc);
				break;

			case Report::Type::kDamage: {
				if (!g_current) {
					// one begins when a fighter strikes or is struck, not for a fall or a trap
					if (!b || b == a || !PlayerSideInvolved(a, b, pc)) return;
					Begin(a_r.at);
				}
				JoinPair(a, b, pc);
				if (!InBattle(a)) return;
				g_current->Damage(a_r.a, attacker, a_r.amount, a_r.pct, a_r.at);
				g_live[a_r.a].damageSince += a_r.amount;
				g_quiet = 0.0;
				SKSE::log::debug("damage: {:08X} took {:.1f} from {:08X}, health now {:.0f}%", a_r.a, a_r.amount, attacker, a_r.pct * 100.0f);
				break;
			}

			case Report::Type::kDeath:
				if (!g_current) {
					if (!PlayerSideInvolved(a, b, pc)) return;
					Begin(a_r.at);  // a kill out of nowhere (a sneak attack) is still a fight
				}
				JoinPair(a, b, pc);
				if (!InBattle(a)) return;
				g_current->Death(a_r.a, a_r.b, a_r.at);
				g_quiet = 0.0;
				SKSE::log::debug("death: {:08X} killed by {:08X}", a_r.a, a_r.b);
				break;

			case Report::Type::kBleedout:
				if (!g_current || !(InBattle(a) || Belongs(a, nullptr, pc))) return;
				Join(a);
				g_current->Down(a_r.a, a_r.at);
				SKSE::log::debug("bleedout: {:08X} went down", a_r.a);
				break;

			case Report::Type::kEffect: {
				if (!g_current) return;
				if (!InBattle(a) && !Belongs(a, b, pc)) return;
				// what a corpse gives off (other mods' shock, frost and fire effects on the dead) is not its caster's doing
				if (b && b != a && b->IsDead()) return;
				auto* effect = RE::TESForm::LookupByID<RE::EffectSetting>(a_r.effect);
				if (!effect) return;
				const auto on = FindOnTarget(a, effect, a_r.b);
				const auto kind = Effects::Classify(effect, on.item, b == a);
				if (!kind) return;
				// harm someone does to themself is their own spell's price or another mod's bookkeeping, not the battle's
				if (kind->hostile && b == a) return;
				JoinPair(a, b, pc);
				if (!InBattle(a)) return;
				SKSE::log::debug("effect: {:08X} {} by {:08X} ({}){}", a_r.a, kind->label, a_r.b, on.source, kind->heal ? " [heal]" : "");
				// no caster: a potion heals its drinker; a hostile effect from nobody is a trap or a hazard
				if (kind->heal) g_recentHeals.push_back({ a_r.a, b ? a_r.b : a_r.a, on.source, a_r.at });
				else g_current->Effect(a_r.a, a_r.b, kind->label, on.source, kind->hostile);
				break;
			}
			}
		}

		void Drain()
		{
			std::vector<Report> reports;
			{
				std::scoped_lock l{ g_queueLock };
				reports.swap(g_queue);
			}
			if (reports.size() > Perf::queueHigh.load(std::memory_order_relaxed)) Perf::queueHigh = reports.size();
			g_knownListed = false;
			for (const auto& r : reports) Handle(r);
			g_known.clear();
			g_knownListed = false;
		}

		// who is healing a_actor right now: restore-health effects on them, and ones that landed in the last moments
		std::vector<std::pair<ActorId, std::string>> HealersOf(RE::Actor* a_actor, double a_now)
		{
			std::vector<std::pair<ActorId, std::string>> out;
			const auto id = a_actor->GetFormID();
			const auto add = [&](ActorId a_healer, const std::string& a_source) {
				if (a_healer == 0) a_healer = id;
				if (std::ranges::find_if(out, [&](const auto& h) { return h.first == a_healer; }) == out.end()) out.emplace_back(a_healer, a_source);
			};
			ForEachEffect(a_actor, [&](RE::ActiveEffect* ae) {
				if (ae->flags.any(RE::ActiveEffect::Flag::kInactive, RE::ActiveEffect::Flag::kDispelled)) return;
				const auto* base = ae->GetBaseObject();
				if (Effects::RestoresHealth(base)) add(CasterOf(ae), SourceName(ae->spell, base));
			});
			for (const auto& h : g_recentHeals) {
				if (h.target == id && a_now - h.at <= 1.5) add(h.healer, h.source);
			}
			return out;
		}

		// post the remembered event once the battle can no longer resume
		std::vector<std::string> DueMemories(double a_now)
		{
			std::vector<std::string> out;
			const auto&              cfg = Settings::Get();
			if (!cfg.rememberEvent || g_current) return out;
			for (auto& b : g_history) {
				auto& extra = g_extra[b.id];
				if (extra.remembered || a_now - b.endedAt <= cfg.mergeGapSeconds) continue;
				extra.remembered = true;
				if (Significant(b, cfg.minEnemies, cfg.minSeconds)) {
					out.push_back(BuildMemory(b, { cfg.showNumbers, cfg.maxOthers, cfg.maxEffects }));
				}
			}
			return out;
		}

		// true when a battle was underway for this tick
		bool Tick()
		{
			auto* pc = RE::PlayerCharacter::GetSingleton();
			auto* ui = RE::UI::GetSingleton();
			const auto& cfg = Settings::Get();
			if (!pc || !ui || !pc->Is3DLoaded() || !cfg.enabled) return false;
			bool battle = false;

			const double now = Now();
			std::vector<std::string> memories;
			{
				std::scoped_lock l{ g_lock };
				const double dt = std::clamp(now - g_lastTick, 0.0, 1.0);
				g_lastTick = now;
				if (ui->GameIsPaused()) return false;

				Drain();
				if (!g_current && pc->IsInCombat()) {
					Begin(now);
					Join(pc);
				}
				if (g_current) {
					battle = true;
					if (g_current->participants.size() > Perf::mostParticipants.load(std::memory_order_relaxed)) Perf::mostParticipants = g_current->participants.size();
					bool fighting = pc->IsInCombat();
					// by index: Join() below may grow the list
					for (std::size_t i = 0; i < g_current->participants.size(); ++i) {
						const auto id = g_current->participants[i].info.id;
						auto&      live = g_live[id];
						const auto ref = live.handle.get();
						auto*      actor = ref.get();
						if (!actor) continue;
						if (g_current->participants[i].dead) continue;
						if (actor->IsDead()) {
							g_current->Death(id, 0, now);  // a death the game did not report
							continue;
						}

						const float hp = HealthOf(actor), max = MaxHealthOf(actor);
						g_current->participants[i].info.maxHealth = max;
						g_current->Sample(id, hp / max, now);

						if (actor->AsActorState()->IsBleedingOut()) {
							if (!g_current->participants[i].isDown) SKSE::log::debug("bleedout: {:08X} is down (seen at the tick), health {:.0f}/{:.0f}", id, hp, max);
							g_current->Down(id, now);
						} else {
							g_current->Up(id);
						}

						// What the damage reports do not explain. Health lost beyond them is damage that never came through
						// the hook (reports can also arrive a tick late, so a loss waits one tick before it is counted);
						// health gained beyond them is healing, when someone or something is doing it.
						if (live.lastHealth >= 0.0f) {
							const float unexplained = (live.lastHealth - hp) - live.damageSince;  // + lost, - regained
							float       gained = 0.0f;
							if (unexplained < 0.0f) {
								const float late = std::min(live.pendingLoss, -unexplained);  // last tick's loss, reported now
								live.pendingLoss -= late;
								gained = -unexplained - late;
							}
							if (live.pendingLoss > 0.02f * max) {
								SKSE::log::debug("damage: {:08X} lost {:.1f} health that no report explained; counted as nobody's blow", id, live.pendingLoss);
								g_current->Damage(id, 0, live.pendingLoss, hp / max, now);
							}
							live.pendingLoss = unexplained > 0.0f ? unexplained : 0.0f;
							if (gained > 0.5f) {
								const auto healers = HealersOf(actor, now);
								if (!healers.empty()) {
									SKSE::log::debug("heal: {:08X} regained {:.1f} from {} healer(s), first {:08X} ({})", id, gained, healers.size(), healers.front().first, healers.front().second);
								}
								for (const auto& [healer, source] : healers) {
									if (auto* h = Lookup(healer); h && h != actor && Near(h)) Join(h);
									g_current->Heal(id, healer, gained / static_cast<float>(healers.size()), source, now);
								}
							}
						}
						live.lastHealth = hp;
						live.damageSince = 0.0f;

						if (actor->IsInCombat()) {
							fighting = true;
							// whoever they are fighting is in this battle too, even before a blow lands
							if (const auto target = actor->GetActorRuntimeData().currentCombatTarget.get(); target && !InBattle(target.get()) && Belongs(target.get(), actor, pc)) {
								Join(target.get());
							}
						}
					}
					std::erase_if(g_recentHeals, [&](const RecentHeal& h) { return now - h.at > 3.0; });

					g_quiet = fighting ? 0.0 : g_quiet + dt;
					if (g_quiet >= cfg.endGraceSeconds) End(now - g_quiet);
					else if (now - g_current->startedAt > 3600.0) End(now);  // something kept it open: close it
				}
				memories = DueMemories(now);
			}
			for (const auto& m : memories) SkyrimNet::Remember(m);  // outside the lock: this goes through Papyrus
			return battle;
		}

		// ---- the game's events ----
		struct Sink final :
			RE::BSTEventSink<RE::TESCombatEvent>,
			RE::BSTEventSink<RE::TESDeathEvent>,
			RE::BSTEventSink<RE::TESEnterBleedoutEvent>,
			RE::BSTEventSink<RE::TESMagicEffectApplyEvent>
		{
			static Sink* Get()
			{
				static Sink s;
				return &s;
			}

			static ActorId Id(const RE::NiPointer<RE::TESObjectREFR>& a_ref)
			{
				return a_ref && a_ref->Is(RE::FormType::ActorCharacter) ? a_ref->GetFormID() : 0;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent* a_e, RE::BSTEventSource<RE::TESCombatEvent>*) override
			{
				Perf::Scope timed{ Perf::eventSink };
				if (a_e && a_e->newState == RE::ACTOR_COMBAT_STATE::kCombat && Id(a_e->actor)) {
					Push({ Report::Type::kCombat, Id(a_e->actor), Id(a_e->targetActor) });
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_e, RE::BSTEventSource<RE::TESDeathEvent>*) override
			{
				Perf::Scope timed{ Perf::eventSink };
				if (a_e && Id(a_e->actorDying)) Push({ Report::Type::kDeath, Id(a_e->actorDying), Id(a_e->actorKiller) });
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESEnterBleedoutEvent* a_e, RE::BSTEventSource<RE::TESEnterBleedoutEvent>*) override
			{
				Perf::Scope timed{ Perf::eventSink };
				if (a_e && Id(a_e->actor)) Push({ Report::Type::kBleedout, Id(a_e->actor) });
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent* a_e, RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override
			{
				Perf::Scope timed{ Perf::eventSink };
				if (a_e && a_e->magicEffect && Id(a_e->target)) {
					Report r{ Report::Type::kEffect, Id(a_e->target), Id(a_e->caster) };
					r.effect = a_e->magicEffect;
					Push(r);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// Actor::HandleHealthDamage: the one place every loss of health passes through with its attacker and its
		// amount (a hit event carries neither). Called after the health has changed.
		//
		// The engine also calls this slot for something that is not damage at all: when the player activates a crafting
		// station, TESFurniture's activation calls it on the player with the furniture reference where the attacker
		// would be, and treats what comes back in AL as "in combat, refuse" (SkyrimVR.exe+22BE0A, read from
		// the running game). A second call in the same function (+22BF3A, reached when the player tries to use furniture
		// someone already occupies) passes a small integer there instead. So the second argument is not always an actor,
		// not even always a pointer: it must never be dereferenced in the hook (doing so crashed the game), and the
		// original's return value must reach the caller untouched: see the thunks below.
		void OnHealthDamage(RE::Actor* a_target, void* a_source, float a_damage)
		{
			Perf::Scope timed{ Perf::damageHook };
			if (!a_target || !Settings::Get().enabled) return;
			const float amount = std::fabs(a_damage);
			if (!(amount > 0.01f) || amount > 1.0e6f) return;
			// a_source is only carried as a number here: it is resolved against the actors that exist when the report is
			// worked through (ResolveSource), because it is not always a pointer at all
			Report r{ Report::Type::kDamage, a_target->GetFormID(), 0, amount };
			r.source = reinterpret_cast<std::uintptr_t>(a_source);
			r.pct = HealthPct(a_target);
			Push(r);
		}

		// The thunks hand back whatever the original left in RAX. CommonLib declares this function void, but at least one
		// caller reads its result; a thunk that returned nothing left that caller with whatever our own code had last put
		// in AL, and the player could no longer use any crafting station ("You cannot use this while in combat").
		struct CharacterDamage
		{
			static std::uintptr_t thunk(RE::Actor* a_this, void* a_source, float a_damage)
			{
				const std::uintptr_t result = func(a_this, a_source, a_damage);
				OnHealthDamage(a_this, a_source, a_damage);
				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct PlayerDamage
		{
			static std::uintptr_t thunk(RE::Actor* a_this, void* a_source, float a_damage)
			{
				const std::uintptr_t result = func(a_this, a_source, a_damage);
				OnHealthDamage(a_this, a_source, a_damage);
				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		const Battle* BattleFor(ActorId a_viewer, bool& a_witness)
		{
			a_witness = false;
			if (g_current) {
				if (g_current->Find(a_viewer)) return &*g_current;
			}
			const float hours = GameHours();
			for (const auto& b : g_history) {
				if (hours - b.endedGameHours > Settings::Get().summaryGameHours) continue;
				if (b.Find(a_viewer)) return &b;
			}
			// not in any of them: the latest one they stood near
			if (g_current) {
				auto* a = Lookup(a_viewer);
				auto* pc = RE::PlayerCharacter::GetSingleton();
				if (a && pc && a->GetPosition().GetDistance(pc->GetPosition()) <= Settings::Get().witnessRange) {
					a_witness = true;
					return &*g_current;
				}
			}
			for (const auto& b : g_history) {
				if (hours - b.endedGameHours > Settings::Get().summaryGameHours) continue;
				const auto it = g_extra.find(b.id);
				if (it != g_extra.end() && it->second.witnesses.contains(a_viewer)) {
					a_witness = true;
					return &b;
				}
			}
			return nullptr;
		}

		const char* AgeText(const Battle& a_b)
		{
			if (a_b.Ongoing()) return "now";
			const float h = GameHours() - a_b.endedGameHours;
			if (h < 0.25f) return "moments ago";
			if (h < 1.0f) return "a short while ago";
			if (h < 3.0f) return "an hour or two ago";
			return "some hours ago";
		}

		Summary SummaryFor(RE::Actor* a_viewer, std::string& a_age)
		{
			Perf::Scope timed{ Perf::summary };
			Summary r;
			if (!a_viewer) return r;
			const auto        id = a_viewer->GetFormID();
			const char*       n = a_viewer->GetDisplayFullName();
			const std::string name = n && *n ? n : "someone";
			const auto&       cfg = Settings::Get();

			std::scoped_lock l{ g_lock };
			bool             witness = false;
			const auto*      b = BattleFor(id, witness);
			if (!b) return r;
			if (b->participants.size() < 2) return r;  // just begun: nothing to tell yet
			r = BuildSummary(*b, id, name, { cfg.showNumbers, cfg.maxOthers, cfg.maxEffects });
			a_age = AgeText(*b);
			return r;
		}
	}

	void Install()
	{
		if (g_installed.exchange(true)) return;

		auto* src = RE::ScriptEventSourceHolder::GetSingleton();
		src->AddEventSink<RE::TESCombatEvent>(Sink::Get());
		src->AddEventSink<RE::TESDeathEvent>(Sink::Get());
		src->AddEventSink<RE::TESEnterBleedoutEvent>(Sink::Get());
		src->AddEventSink<RE::TESMagicEffectApplyEvent>(Sink::Get());

		// HandleHealthDamage is vfunc 0x104 on SE/AE and 0x106 on VR (CommonLibVR's Actor.h: VR's table gains two slots
		// before it).
		const auto slot = REL::Relocate(0x104, 0x104, 0x106);
		REL::Relocation<std::uintptr_t> character{ RE::VTABLE_Character[0] };
		REL::Relocation<std::uintptr_t> player{ RE::VTABLE_PlayerCharacter[0] };
		CharacterDamage::func = character.write_vfunc(slot, CharacterDamage::thunk);
		PlayerDamage::func = player.write_vfunc(slot, PlayerDamage::thunk);
		SKSE::log::info("Tracker: sinks registered, health-damage hook on vtable slot {:#x}", slot);

		// The tick: a plain timer thread that asks SKSE to run Tick() on the main thread. It holds no lock while it
		// waits, and it is not a render hook (on VR, queueing SKSE tasks from a render hook can deadlock the task queue).
		std::thread([] {
			for (;;) {
				std::this_thread::sleep_for(std::chrono::milliseconds(250));
				SKSE::GetTaskInterface()->AddTask([] {
					Perf::Scope timed{ Perf::tickIdle };
					if (Tick()) timed.Into(Perf::tickBattle);
				});
			}
		}).detach();
	}

	void Reset()
	{
		{
			std::scoped_lock l{ g_queueLock };
			g_queue.clear();
		}
		std::scoped_lock l{ g_lock };
		g_current.reset();
		g_history.clear();
		g_extra.clear();
		g_live.clear();
		g_recentHeals.clear();
		g_quiet = 0.0;
		g_lastMemory.clear();
	}

	std::string SummaryJson(RE::Actor* a_viewer)
	{
		std::string age;
		const auto  r = SummaryFor(a_viewer, age);
		nlohmann::json j;
		j["show"] = r.show;
		j["ongoing"] = r.ongoing;
		j["participant"] = r.participant;
		j["name"] = r.name;
		j["age"] = age;
		j["general"] = r.general;
		j["personal"] = r.personal;
		j["others"] = r.others;
		return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	}

	std::string SummaryPlain(RE::Actor* a_viewer)
	{
		std::string age;
		const auto  r = SummaryFor(a_viewer, age);
		return SummaryText(r);
	}

	std::string Status()
	{
		std::scoped_lock l{ g_lock };
		std::string      s = std::format("enabled {}, installed {}, history {}", Settings::Get().enabled, g_installed.load(), g_history.size());
		if (g_current) {
			s += std::format("; battle {} underway at '{}', {} participants, quiet {:.1f}s:", g_current->id, g_current->location, g_current->participants.size(), g_quiet);
			for (const auto& p : g_current->participants) {
				s += std::format(" [{} {:08X} side {} taken {:.0f} dealt {:.0f} healed {:.0f} min {:.0f}% downs {}{}]", p.info.name, p.info.id,
					static_cast<int>(g_current->EffectiveSide(p)), p.damageTaken, p.damageDealt, p.healingReceived, p.minHealthPct * 100.0f, p.downs, p.dead ? " dead" : "");
			}
		} else {
			s += "; no battle underway";
		}
		for (const auto& b : g_history) {
			s += std::format("\nbattle {} at '{}', {:.0f}s:", b.id, b.location, b.endedAt - b.startedAt);
			for (const auto& p : b.participants) {
				s += std::format(" [{} {:08X} side {}{} taken {:.0f}/{:.0f} dealt {:.0f} healed {:.0f} min {:.0f}% downs {}{}{}]", p.info.name, p.info.id,
					static_cast<int>(b.EffectiveSide(p)), b.Involved(p) ? "" : " uninvolved", p.damageTaken, p.info.maxHealth, p.damageDealt, p.healingReceived,
					p.minHealthPct * 100.0f, p.downs, p.nearDeath ? " neardeath" : "", p.dead ? " dead" : "");
			}
		}
		return s;
	}

	std::string LastMemory()
	{
		std::scoped_lock l{ g_lock };
		return g_lastMemory;
	}

	void EndNow()
	{
		std::scoped_lock l{ g_lock };
		if (g_current) End(Now());
	}

	std::string FakeRescue(RE::Actor* a_victim)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!a_victim || !pc || a_victim == pc) return "needs an NPC";
		const double now = Now();
		std::scoped_lock l{ g_lock };
		if (g_current) End(now);

		constexpr ActorId kChief = 0xFFFFFF01, kBandit = 0xFFFFFF02;  // not real references: they only exist in this record
		Battle            b;
		b.id = g_nextId++;
		b.startedAt = now - 45.0;
		b.location = WhereNow();
		b.nearDeathPct = Settings::Get().nearDeathPct;
		auto me = InfoOf(pc);
		auto them = InfoOf(a_victim);
		them.side = Side::kPlayer;
		b.Join(me);
		b.Join(them);
		b.Join({ kChief, "Bandit Chief", Side::kEnemy, false, 0, 400.0f });
		b.Join({ kBandit, "Bandit", Side::kEnemy, false, 0, 120.0f });
		b.Damage(kBandit, them.id, 120.0f, 0.0f, now - 40.0);
		b.Death(kBandit, them.id, now - 40.0);
		b.Effect(them.id, kChief, "poisoned", "Lingering Damage Health", true);
		b.Damage(them.id, kChief, them.maxHealth * 0.5f, 0.5f, now - 30.0);
		b.Damage(them.id, kChief, them.maxHealth * 0.45f, 0.05f, now - 26.0);
		b.Down(them.id, now - 26.0);
		b.Damage(me.id, kChief, me.maxHealth * 0.15f, 0.85f, now - 22.0);
		b.Damage(kChief, me.id, 400.0f, 0.0f, now - 15.0);
		b.Death(kChief, me.id, now - 15.0);
		b.Heal(them.id, me.id, them.maxHealth * 0.8f, "Healing Hands", now - 10.0);
		b.Up(them.id);
		b.endedAt = now - 8.0;
		b.endedGameHours = GameHours();

		auto& extra = g_extra[b.id];
		extra.remembered = true;  // a test record is never written into SkyrimNet's memory
		g_lastMemory = BuildMemory(b, {});
		g_history.push_front(std::move(b));
		while (g_history.size() > kHistory) {
			g_extra.erase(g_history.back().id);
			g_history.pop_back();
		}
		return g_lastMemory;
	}
}
