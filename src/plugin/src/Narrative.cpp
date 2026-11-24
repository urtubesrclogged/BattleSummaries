#include "Narrative.h"

#include <map>

#include "Text.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <set>

namespace BSM
{
	namespace
	{
		// Every word told comes from Text (the player can reword it): T() for a phrase as it stands, F() to fill in its
		// {placeholders}. What a placeholder may leave empty carries its own leading space: Sp().
		std::string T(std::string_view a_key) { return Text::Get(a_key); }
		std::string F(std::string_view a_key, std::initializer_list<Text::Arg> a_args) { return Text::Fill(a_key, a_args); }
		std::string Sp(const std::string& a_text) { return a_text.empty() ? std::string() : " " + a_text; }
		std::string N(std::size_t a_n) { return std::to_string(a_n); }
		std::string N(int a_n) { return std::to_string(a_n); }

		// "a", "a and b", "a, b and c"
		std::string JoinList(const std::vector<std::string>& a_items)
		{
			std::string out;
			const auto  last = " " + T("list.and") + " ";
			for (std::size_t i = 0; i < a_items.size(); ++i) {
				if (i > 0) out += (i + 1 == a_items.size()) ? last : std::string(", ");
				out += a_items[i];
			}
			return out;
		}

		// Several fighters of one name are told apart by the order they joined: "the first Troll", "the second Troll". The
		// same one keeps its number in every line and every NPC's account, so a reader can follow who did what. Lists keep
		// the name alone ("Troll x2"); a unique name is never numbered.
		std::string Numbered(const Battle& a_b, const Participant& a_p)
		{
			int nth = 0, of = 0;
			for (const auto& q : a_b.participants) {
				if (q.info.name != a_p.info.name || !a_b.Involved(q)) continue;
				++of;
				if (&q == &a_p) nth = of;
			}
			if (of < 2 || nth == 0) return a_p.info.name;
			if (nth <= 10) return F("name.nth", { { "ordinal", T("ordinal." + std::to_string(nth)) }, { "name", a_p.info.name } });
			return F("name.numbered", { { "name", a_p.info.name }, { "count", std::to_string(nth) } });
		}

		std::string NameOf(const Battle& a_b, ActorId a_id)
		{
			if (a_id == 0) return T("name.nobody");
			const auto* p = a_b.Find(a_id);
			if (!p || p->info.name.empty()) return T("name.someone");
			return Numbered(a_b, *p);
		}

		// a line begins with a capital, whatever name or phrase it opens with ("the second Troll, who ...")
		std::string Cap(std::string a_line)
		{
			if (!a_line.empty() && a_line[0] >= 'a' && a_line[0] <= 'z') a_line[0] = static_cast<char>(a_line[0] - 'a' + 'A');
			return a_line;
		}

		std::string CreditName(const Battle& a_b, ActorId a_id);

		// "was killed by Bandit Chief"; "was killed, by whom is not known" when the killer is nobody the battle saw
		std::string KilledBy(const Battle& a_b, ActorId a_killer)
		{
			if (a_killer == 0 || !a_b.Find(a_killer)) return T("killed.unknown");
			return F("killed.by", { { "killer", CreditName(a_b, a_killer) } });
		}

		// for kill and deed credit: a summon is named with its master
		std::string CreditName(const Battle& a_b, ActorId a_id)
		{
			const auto* p = a_b.Find(a_id);
			if (p && p->info.master != 0 && a_b.Find(p->info.master)) {
				return F("name.commanded", { { "name", NameOf(a_b, a_id) }, { "master", NameOf(a_b, p->info.master) } });
			}
			return NameOf(a_b, a_id);
		}

		// "Bandit x3, Bandit Chief": same-named actors are counted, in first-seen order; past a_max names, "and N others"
		std::string Grouped(const std::vector<const Participant*>& a_ps, std::size_t a_max = 4)
		{
			std::vector<std::pair<std::string, int>> groups;
			for (const auto* p : a_ps) {
				const auto n = p->info.name.empty() ? T("name.someone") : p->info.name;
				const auto  it = std::ranges::find_if(groups, [&](const auto& g) { return g.first == n; });
				if (it == groups.end()) groups.emplace_back(n, 1);
				else ++it->second;
			}
			std::vector<std::string> items;
			int                      rest = 0;
			for (const auto& [n, c] : groups) {
				// one name over the limit is shorter told than "and 1 other"
				if (items.size() < a_max || groups.size() == a_max + 1) items.push_back(c > 1 ? F("list.times", { { "name", n }, { "count", N(c) } }) : n);
				else rest += c;
			}
			if (rest > 0) items.push_back(F("list.others", { { "count", N(rest) } }));
			return JoinList(items);
		}

		std::vector<const Participant*> OnSide(const Battle& a_b, Side a_side)
		{
			std::vector<const Participant*> out;
			for (const auto& p : a_b.participants) {
				if (!a_b.Involved(p) || a_b.EffectiveSide(p) != a_side) continue;
				// a summon on the player's side (or an ally's) is its master's doing, not a fighter of its own: its kills are told
				// with the master's, and its end (a dismissal as often as a death) is never a loss
				if ((a_side == Side::kPlayer || a_side == Side::kAlly) && p.info.summon) continue;
				out.push_back(&p);
			}
			std::ranges::stable_partition(out, [](const Participant* p) { return p->info.isPlayer; });  // the player leads the list
			return out;
		}

		// Caught in the crossfire of a fight with real enemies: on neither side, never hostile to the player (Join would
		// have made them an enemy) and struck nobody. With no enemies about, whoever the party went for was no accident
		// (a townsman attacked on his own is told as attacked).
		bool Bystander(const Battle& a_b, const Participant& a_p)
		{
			return a_p.info.side == Side::kOther && a_p.damageDealt <= 0.0f && a_b.HasEnemies() && a_b.EffectiveSide(a_p) == Side::kOther;
		}

		// The same side, or the player's side and an ally: what one does to the other is an accident, not an attack.
		bool Friends(Side a_x, Side a_y)
		{
			const auto ours = [](Side s) { return s == Side::kPlayer || s == Side::kAlly; };
			return a_x == a_y || (ours(a_x) && ours(a_y));
		}

		// someone else, on the same side (or the party and an ally): what they did to a_p was friendly fire. So is what the
		// player's side did to a bystander (a townsman at the edge of a fireball aimed at bandits): the same accident, told
		// or left out the same way.
		bool OwnSide(const Battle& a_b, const Participant& a_p, ActorId a_other)
		{
			const auto* q = a_b.Find(a_other);
			if (!q || q->info.id == a_p.info.id) return false;
			const Side theirs = a_b.EffectiveSide(*q);
			return Friends(theirs, a_b.EffectiveSide(a_p)) || ((theirs == Side::kPlayer || theirs == Side::kAlly) && Bystander(a_b, a_p));
		}

		// the damage a participant did to the other side: what they did to their own does not make them a fighter
		float DealtToFoes(const Battle& a_b, const Participant& a_p)
		{
			const Side side = a_b.EffectiveSide(a_p);
			float      sum = 0.0f;
			for (const auto& q : a_b.participants) {
				if (q.info.id == a_p.info.id || Friends(a_b.EffectiveSide(q), side)) continue;
				if (const auto it = q.damageFrom.find(a_p.info.id); it != q.damageFrom.end()) sum += it->second;
			}
			return sum;
		}

		// with what their summons did: Scrap's bites are Remiel's share
		float PartyDealt(const Battle& a_b, const Participant& a_p)
		{
			float sum = DealtToFoes(a_b, a_p);
			if (a_p.info.summon) return sum;
			for (const auto& q : a_b.participants) {
				if (q.info.summon && q.info.master == a_p.info.id) sum += DealtToFoes(a_b, q);
			}
			return sum;
		}

		float SideDealtToFoes(const Battle& a_b, Side a_side)
		{
			float sum = 0.0f;
			for (const auto& p : a_b.participants) {
				if (a_b.EffectiveSide(p) == a_side) sum += DealtToFoes(a_b, p);
			}
			return sum;
		}

		Side Opposing(Side a_side) { return a_side == Side::kEnemy ? Side::kPlayer : Side::kEnemy; }

		std::string Points(float a_amount, const NarrativeOptions& a_opt)
		{
			return a_opt.showNumbers ? " " + F("points", { { "points", N(static_cast<int>(std::lround(a_amount))) } }) : std::string();
		}


		// the damage their own side did to them, and who did a part of it worth naming
		float FriendlyDamage(const Battle& a_b, const Participant& a_p, std::vector<ActorId>* a_by = nullptr)
		{
			float sum = 0.0f;
			for (const auto& [id, dmg] : a_p.damageFrom) {
				if (id == 0 || !OwnSide(a_b, a_p, id)) continue;
				sum += dmg;
				if (a_by && dmg >= 0.05f * a_p.info.maxHealth) a_by->push_back(id);
			}
			return sum;
		}

		std::string Wounds(const Battle& a_b, const Participant& a_p, const NarrativeOptions& a_opt)
		{
			// with friendly fire left out, what their own side did to them is no part of the wounds told
			const float taken = a_opt.ignoreFriendlyFire ? std::max(0.0f, a_p.damageTaken - FriendlyDamage(a_b, a_p)) : a_p.damageTaken;
			const float r = a_p.info.maxHealth > 0.0f ? taken / a_p.info.maxHealth : 0.0f;
			std::string w;
			if (r < 0.03f) w = T("wounds.none");
			else if (r < 0.25f) w = T("wounds.light");
			else if (r < 0.6f) w = T("wounds.solid");
			else if (r < 1.0f) w = T("wounds.bad");
			else if (a_p.healingReceived > 0.0f) w = T("wounds.beyond_healed");
			else w = T("wounds.brutal");
			return w + Points(taken, a_opt);
		}

		// a fighter's part of a category that earns them a mention in it, in a party of up to five
		constexpr float kRoleShare = 0.2f;
		// standing out: at least this many times an even share
		constexpr float kStandOut = 1.2f;
		// names told in one line before "and N others"
		constexpr std::size_t kMaxNames = 3;

		// what each of a_n would have had in an even split
		float EvenShare(std::size_t a_n) { return a_n > 0 ? 1.0f / static_cast<float>(a_n) : 1.0f; }
		// the part that earns a mention: a fifth, or an even share in a party too large for anyone to reach a fifth
		float Line(std::size_t a_n) { return std::min(kRoleShare, EvenShare(a_n)); }

		// a list of names kept short: past kMaxNames, the rest as "N others" (one over is shorter told by name)
		std::string Names(const std::vector<std::string>& a_names)
		{
			if (a_names.size() <= kMaxNames + 1) return JoinList(a_names);
			std::vector<std::string> items(a_names.begin(), a_names.begin() + kMaxNames);
			items.push_back(F("list.others", { { "count", N(static_cast<int>(a_names.size() - kMaxNames)) } }));
			return JoinList(items);
		}

		std::string Dealt(const Battle& a_b, const Participant& a_p, const NarrativeOptions& a_opt)
		{
			const Side  side = a_b.EffectiveSide(a_p);
			// an ally's part is of all who fought the enemy, the party with them: alone among the allies is not "all of it"
			const bool  ally = side == Side::kAlly;
			const float total = SideDealtToFoes(a_b, side) + (ally ? SideDealtToFoes(a_b, Side::kPlayer) : 0.0f);
			const float dealt = PartyDealt(a_b, a_p);
			if (dealt <= 0.0f) return T("dealt.none");
			std::string w;
			const auto  n = OnSide(a_b, side).size() + (ally ? OnSide(a_b, Side::kPlayer).size() : 0);
			if (n <= 1 || total <= 0.0f) w = T("dealt.all");
			else {
				// against what each would have done in an even split, so a fair part of a large party's work is not "small"
				const float s = dealt / total, r = s * static_cast<float>(n);
				if (r < 0.5f) w = T("dealt.small");
				else if (r < kStandOut) w = T("dealt.fair");
				else if (s >= 0.5f) w = T("dealt.most");
				else w = T("dealt.large");
			}
			return w + Points(dealt, a_opt);
		}

		std::string HealAmount(const Participant& a_p, float a_amount, const NarrativeOptions& a_opt)
		{
			const float r = a_p.info.maxHealth > 0.0f ? a_amount / a_p.info.maxHealth : 0.0f;
			const auto w = T(r < 0.3f ? "heal.little" : r < 0.7f ? "heal.good" : "heal.most");
			return F("heal.restoring", { { "amount", w }, { "name", a_p.info.name }, { "points", Points(a_amount, a_opt) } });
		}

		// Who pulled a participant back from the brink, and how.
		struct Rescue
		{
			std::vector<ActorId>     saviors;
			std::vector<std::string> deeds;  // full sentences
			std::vector<std::string> brief;  // "A killed X", "A healed Y", for the summary
		};

		Rescue FindRescue(const Battle& a_b, const Participant& a_p, const NarrativeOptions& a_opt)
		{
			Rescue r;
			// put back on their feet by something of their own (a death-alternative mod): nobody else's doing saved them
			if (!a_p.WasCritical() || a_p.dead || a_p.roseAt >= 0.0) return r;
			const auto addSavior = [&](ActorId a_id) {
				if (a_id != 0 && a_id != a_p.info.id && std::ranges::find(r.saviors, a_id) == r.saviors.end()) r.saviors.push_back(a_id);
			};
			const Side  mine = a_b.EffectiveSide(a_p);
			const auto  what = T(a_p.downs > 0 ? "rescue.struck_down" : "rescue.nearly_killed");

			// the assailants: whoever dealt the blows that did it, and anyone who dealt a large part of all the damage
			std::vector<ActorId> assailants;
			if (a_p.broughtLowBy != 0) assailants.push_back(a_p.broughtLowBy);
			for (const auto& [id, dmg] : a_p.damageFrom) {
				if (id != 0 && dmg >= 0.4f * a_p.damageTaken && std::ranges::find(assailants, id) == assailants.end()) assailants.push_back(id);
			}
			for (const auto id : assailants) {
				const auto* a = a_b.Find(id);
				if (!a || !a->dead || a->diedAt < a_p.criticalAt || Friends(a_b.EffectiveSide(*a), mine)) continue;
				// the one whose blows did it "had struck down" them; another who only did a large part of the damage had wounded them
				const bool downedThem = id == a_p.broughtLowBy;
				const auto assailant = NameOf(a_b, a->info.id);
				if (a->killer == a_p.info.id) {
					r.deeds.push_back(Cap(F(downedThem ? "rescue.recovered" : "rescue.recovered_wounded", { { "name", a_p.info.name }, { "assailant", assailant }, { "what", what } })));
				} else if (a->killer != 0) {
					r.deeds.push_back(Cap(F(downedThem ? "rescue.assailant_killed" : "rescue.assailant_wounded",
						{ { "assailant", assailant }, { "what", what }, { "name", a_p.info.name }, { "killer", CreditName(a_b, a->killer) } })));
					// a foe who happened to kill the assailant (monsters turning on each other) saved nobody
					const auto* k = a_b.Find(a->killer);
					if (!k || a_b.EffectiveSide(*k) == Opposing(mine)) continue;
					r.brief.push_back(F("rescue.brief_killed", { { "killer", NameOf(a_b, a->killer) }, { "assailant", assailant } }));
					addSavior(k->info.master != 0 && a_b.Find(k->info.master) ? k->info.master : a->killer);
				}
			}
			for (const auto& [healer, h] : a_p.healFrom) {
				if (healer == 0 || healer == a_p.info.id || h.whileCritical < 0.15f * a_p.info.maxHealth) continue;
				r.deeds.push_back(Cap(F("rescue.healed_while",
					{ { "name", a_p.info.name },
						{ "state", T(a_p.downs > 0 ? "rescue.state_down" : "rescue.state_deaths_door") },
						{ "healer", NameOf(a_b, healer) },
						{ "with", h.source.empty() ? std::string() : " " + F("heal.with", { { "source", h.source } }) },
						{ "restoring", HealAmount(a_p, h.whileCritical, a_opt) } })));
				r.brief.push_back(F("rescue.brief_healed", { { "healer", NameOf(a_b, healer) }, { "name", a_p.info.name } }));
				addSavior(healer);
			}
			return r;
		}

		std::vector<std::string> Names(const Battle& a_b, const std::vector<ActorId>& a_ids)
		{
			std::vector<std::string> out;
			for (const auto id : a_ids) out.push_back(NameOf(a_b, id));
			return out;
		}

		// "was struck down twice by X: collapsed and helpless for a moment, but got back up" / "was brought to the very
		// edge of death by X, ..., but survived" / "". a_recovery: how they came back from it, unless the sentence goes on
		// to say who saved them.
		std::string Brink(const Battle& a_b, const Participant& a_p, const NarrativeOptions& a_opt, bool a_recovery = true)
		{
			const auto recovery = [&](const char* a_key) { return a_recovery ? T(a_p.roseAt >= 0.0 ? "brink.rose" : a_key) : std::string(); };
			std::string by = a_p.broughtLowBy != 0 ? " " + F("brink.by", { { "name", NameOf(a_b, a_p.broughtLowBy) } }) : std::string();
			if (OwnSide(a_b, a_p, a_p.broughtLowBy)) by = a_opt.ignoreFriendlyFire ? std::string() : by + " " + T("brink.friendly");
			if (a_p.downs > 0) {
				const std::string times = a_p.downs == 1 ? std::string() : " " + (a_p.downs == 2 ? T("times.twice") : F("times.many", { { "count", N(a_p.downs) } }));
				return F("brink.down", { { "times", times }, { "by", by } }) + recovery("brink.got_up");
			}
			if (a_p.nearDeath) return F("brink.near_death", { { "by", by } }) + recovery("brink.survived");
			return {};
		}

		// enemy-of-a_side kills per killer, most first: "Kaira 3 (Bandit x2, Bandit Chief), Lydia 1 (Bandit)"
		// a_summons: the summoned creatures among them (told as dispatched), not the rest
		// killed by someone on their own side (a bandit's power attack through another bandit): a real kill, but not one
		// the other side made
		bool OwnSideKill(const Battle& a_b, const Participant& a_v)
		{
			const auto* k = a_b.Find(a_v.killer);
			return k && k->info.id != a_v.info.id && Friends(a_b.EffectiveSide(*k), a_b.EffectiveSide(a_v));
		}

		std::string KillTally(const Battle& a_b, Side a_victims, bool a_summons = false)
		{
			struct Entry
			{
				ActorId                         killer;
				std::vector<const Participant*> victims;
				std::vector<const Participant*> summons;  // the summons that made some of these kills
				int                             bySummons{ 0 };
			};
			std::vector<Entry> tally;
			for (const auto* v : OnSide(a_b, a_victims)) {
				if (!v->dead || v->info.summon != a_summons) continue;
				if (a_summons && v->killer == 0) continue;  // dismissed or expired: nobody's doing
				const auto* k = a_b.Find(v->killer);
				if (OwnSideKill(a_b, *v)) continue;  // told apart (OwnSideDeaths): not one of the other side's killers
				const bool  bySummon = k && k->info.summon && k->info.master != 0 && a_b.Find(k->info.master);
				const auto  credited = bySummon ? k->info.master : v->killer;
				auto        it = std::ranges::find_if(tally, [&](const Entry& t) { return t.killer == credited; });
				if (it == tally.end()) it = tally.insert(tally.end(), { credited, {}, {}, 0 });
				it->victims.push_back(v);
				if (bySummon) ++it->bySummons;
				if (bySummon && std::ranges::none_of(it->summons, [&](const Participant* q) { return q->info.name == k->info.name; })) it->summons.push_back(k);
			}
			std::ranges::stable_sort(tally, [](const Entry& a, const Entry& b) { return a.victims.size() > b.victims.size(); });
			std::vector<std::string> items;
			for (const auto& [killer, victims, summons, bySummons] : tally) {
				// the victims by name only while the list stays short; a summon's share as "through Scrap"
				std::vector<std::string> notes;
				if (victims.size() <= 3) notes.push_back(Grouped(victims));
				if (!summons.empty()) notes.push_back(F("tally.through", { { "count", N(bySummons) }, { "summons", Grouped(summons) } }));
				std::string inner;
				for (const auto& n : notes) inner += (inner.empty() ? "" : ", ") + n;
				items.push_back((killer == 0 ? T("tally.unknown") : CreditName(a_b, killer)) + " " + N(victims.size()) + (inner.empty() ? std::string() : " (" + inner + ")"));
			}
			std::string out;
			for (const auto& i : items) out += (out.empty() ? "" : "; ") + i;
			return out;
		}

		// the foes proper: their summoned creatures are told as dispatched, apart
		int FoesProper(const Battle& a_b, Side a_foes, bool a_deadOnly)
		{
			int n = 0;
			for (const auto* p : OnSide(a_b, a_foes)) {
				if (!p->info.summon && (!a_deadOnly || p->dead)) ++n;
			}
			return n;
		}

		std::string Outcome(const Battle& a_b, Side a_foes)
		{
			const int n = FoesProper(a_b, a_foes, false), dead = FoesProper(a_b, a_foes, true);
			if (n == 0) return {};
			const std::initializer_list<Text::Arg> args{ { "dead", N(dead) }, { "total", N(n) }, { "foes", T(a_foes == Side::kEnemy ? "outcome.word_enemies" : "outcome.word_opponents") } };
			if (a_b.Ongoing()) return F("outcome.so_far", args);
			if (dead == n) return n == 1 ? T("outcome.one") : F(n == 2 ? "outcome.both" : "outcome.all", args);
			if (a_b.cutShort) return (dead > 0 ? F("outcome.cut_short", args) : n == 1 ? T("outcome.cut_short_one") : F("outcome.cut_short_none", args));
			if (dead == 0) return n == 1 ? T("outcome.none_one") : F("outcome.none", args);
			return F("outcome.some", args);
		}

		std::string EffectPhrase(const Battle& a_b, const Participant& a_p, const EffectNote& a_e)
		{
			std::string s = a_e.label;
			if (a_e.by != 0 && a_e.by != a_p.info.id) {
				// by name alone: a list of what was done to them, where three wolves doing the same are told once
				const auto* by = a_b.Find(a_e.by);
				s += " " + F("effect.by", { { "name", by && !by->info.name.empty() ? by->info.name : NameOf(a_b, a_e.by) } });
				if (!a_e.source.empty()) s += " (" + a_e.source + ")";
			} else if (!a_e.source.empty()) {
				s += " (" + (a_e.by == 0 ? a_e.source : F("effect.own", { { "source", a_e.source } })) + ")";  // by nobody: a trap, a hazard
			}
			return s;
		}

		std::string Effects(const Battle& a_b, const Participant& a_p, bool a_hostile, int a_max)
		{
			std::vector<std::string> items;
			int                      more = 0;
			for (const auto& e : a_p.effects) {
				if (e.hostile != a_hostile || !a_b.Landed(a_p, e)) continue;
				if (e.hostile && OwnSide(a_b, a_p, e.by)) continue;  // friendly fire: told apart (FriendlyFire), or not at all
				// the same thing from several attackers of one name (three wolves) is told once
				auto phrase = EffectPhrase(a_b, a_p, e);
				if (std::ranges::find(items, phrase) != items.end()) continue;
				if (static_cast<int>(items.size()) < a_max) items.push_back(std::move(phrase));
				else ++more;
			}
			if (items.empty()) return {};
			auto s = JoinList(items);
			if (more > 0) s += ", " + F("effects.more", { { "count", N(more) } });
			return s;
		}

		// "Leg Injury (dealt by Bandit Chief) and Head Injury"; a_carried picks those brought into the battle
		std::string Injuries(const Battle& a_b, const Participant& a_p, bool a_carried, bool a_withDealer = true)
		{
			std::vector<std::string> items;
			for (const auto& i : a_p.injuries) {
				if (i.carried != a_carried) continue;
				items.push_back(a_withDealer && i.by != 0 ? F("injury.dealt_by", { { "injury", i.name }, { "name", NameOf(a_b, i.by) } }) : i.name);
			}
			return JoinList(items);
		}

		// Bodies that lost a head or a limb, per killer: "Lydia beheaded Bandit Chief."
		std::vector<std::string> Maimings(const Battle& a_b)
		{
			struct Group
			{
				ActorId                         killer;
				bool                            beheaded;
				std::vector<const Participant*> victims;
			};
			std::vector<Group> groups;
			for (const auto& p : a_b.participants) {
				if (!p.dead || !p.dismembered || !a_b.Involved(p)) continue;
				const auto it = std::ranges::find_if(groups, [&](const Group& g) { return g.killer == p.killer && g.beheaded == p.beheaded; });
				if (it == groups.end()) groups.push_back({ p.killer, p.beheaded, { &p } });
				else it->victims.push_back(&p);
			}
			std::vector<std::string> out;
			for (const auto& g : groups) {
				if (out.size() >= 4) break;  // a slaughter needs no full list
				const auto who = Grouped(g.victims);
				if (g.killer == 0) out.push_back(F(g.beheaded ? "maim.head_unknown" : "maim.limb_unknown", { { "victims", who } }));
				else out.push_back(F(g.beheaded ? "maim.beheaded" : "maim.dismembered", { { "killer", CreditName(a_b, g.killer) }, { "victims", who } }));
			}
			return out;
		}

		// what their own side's attacks did to them: "burned by fire by Kaira (Fireball)"
		std::string FriendlyFire(const Battle& a_b, const Participant& a_p, int a_max)
		{
			std::vector<std::string> items;
			for (const auto& e : a_p.effects) {
				if (!e.hostile || !a_b.Landed(a_p, e) || !OwnSide(a_b, a_p, e.by) || static_cast<int>(items.size()) >= a_max) continue;
				auto phrase = EffectPhrase(a_b, a_p, e);
				if (std::ranges::find(items, phrase) == items.end()) items.push_back(std::move(phrase));
			}
			return JoinList(items);
		}

		// Several foes killed by one fighter in the same instant (a fireball, a shout, a cleaving blow): "Kaira killed 3
		// at once: Bandit x2 and Bandit Chief." The largest first, three lines at most.
		std::vector<std::string> KilledAtOnce(const Battle& a_b)
		{
			constexpr double kSameInstant = 0.75;  // seconds between deaths that still count as one stroke
			struct Stroke
			{
				ActorId                         killer;
				std::vector<const Participant*> victims;
			};
			std::vector<Stroke> strokes;
			for (const auto& k : a_b.participants) {
				if (k.kills.size() < 2) continue;
				const Side                      foes = Opposing(a_b.EffectiveSide(k));
				std::vector<const Participant*> dead;
				for (const auto id : k.kills) {
					const auto* v = a_b.Find(id);
					if (v && v->dead && !v->info.summon && v->diedAt >= 0.0 && a_b.Involved(*v) && a_b.EffectiveSide(*v) == foes) dead.push_back(v);
				}
				std::ranges::stable_sort(dead, [](const Participant* a, const Participant* b) { return a->diedAt < b->diedAt; });
				for (std::size_t i = 0; i < dead.size();) {
					std::size_t j = i + 1;
					while (j < dead.size() && dead[j]->diedAt - dead[j - 1]->diedAt <= kSameInstant) ++j;
					if (j - i >= 2) strokes.push_back({ k.info.id, { dead.begin() + i, dead.begin() + j } });
					i = j;
				}
			}
			std::ranges::stable_sort(strokes, [](const Stroke& a, const Stroke& b) { return a.victims.size() > b.victims.size(); });
			std::vector<std::string> out;
			for (const auto& st : strokes) {
				if (out.size() >= 3) break;
				// how, when one spell or weapon did all of it
				std::string how = st.victims.front()->killedWith;
				for (const auto* v : st.victims) {
					if (v->killedWith != how) how.clear();
				}
				out.push_back(F("moment.at_once",
					{ { "killer", CreditName(a_b, st.killer) },
						{ "count", N(st.victims.size()) },
						{ "with", how.empty() ? std::string() : " " + F("moment.with", { { "means", how } }) },
						{ "victims", Grouped(st.victims) } }));
			}
			return out;
		}

		// the participant's own kills of the other side, and their summons' (a_bySummons counts those, a_summons names them)
		std::string ItemList(const std::vector<ItemUse>& a_items, int* a_total = nullptr)
		{
			std::vector<std::string> names;
			int                      total = 0;
			for (const auto& u : a_items) {
				if (u.count <= 0) continue;
				total += u.count;
				names.push_back(u.count > 1 ? F("list.times", { { "name", u.name }, { "count", N(u.count) } }) : u.name);
			}
			if (a_total) *a_total = total;
			return JoinList(names);
		}

		// "drank 4 potions", "used a poison": short, for a companion's line
		void ItemBits(const Participant& a_p, std::vector<std::string>& a_bits)
		{
			int n = 0;
			ItemList(a_p.potions, &n);
			if (n == 1) a_bits.push_back(T("other.potion"));
			else if (n > 1) a_bits.push_back(F("other.potions", { { "count", N(n) } }));
			ItemList(a_p.poisons, &n);
			if (n == 1) a_bits.push_back(T("other.poison"));
			else if (n > 1) a_bits.push_back(F("other.poisons", { { "count", N(n) } }));
		}

		int EnemyKills(const Battle& a_b, const Participant& a_p, Side a_foes, std::vector<const Participant*>* a_out = nullptr, int* a_bySummons = nullptr,
			std::vector<const Participant*>* a_summons = nullptr, bool a_summonedVictims = false)
		{
			int        n = 0;
			const auto count = [&](const Participant& a_killer, bool a_summon) {
				for (const auto id : a_killer.kills) {
					const auto* v = a_b.Find(id);
					if (!v || !a_b.Involved(*v) || a_b.EffectiveSide(*v) != a_foes || v->info.summon != a_summonedVictims) continue;
					++n;
					if (a_out) a_out->push_back(v);
					if (!a_summon) continue;
					if (a_bySummons) ++*a_bySummons;
					if (a_summons && std::ranges::none_of(*a_summons, [&](const Participant* q) { return q->info.name == a_killer.info.name; })) a_summons->push_back(&a_killer);
				}
			};
			count(a_p, false);
			if (!a_p.info.summon) {
				for (const auto& q : a_b.participants) {
					if (q.info.summon && q.info.master == a_p.info.id) count(q, true);
				}
			}
			return n;
		}

		std::vector<std::string> Personal(const Battle& a_b, const Participant& a_p, const NarrativeOptions& a_opt)
		{
			std::vector<std::string> out;
			const auto&              n = a_p.info.name;
			const Side               foes = Opposing(a_b.EffectiveSide(a_p));

			out.push_back(F("personal.wounds_dealt", { { "name", n }, { "wounds", Wounds(a_b, a_p, a_opt) }, { "dealt", Dealt(a_b, a_p, a_opt) } }));

			if (a_p.dead) {
				out.push_back(F("killed.line", { { "name", n }, { "killed", KilledBy(a_b, a_p.killer) }, { "own_side", "" } }));
			} else if (const auto brink = Brink(a_b, a_p, a_opt); !brink.empty()) {
				out.push_back(F("personal.fact", { { "name", n }, { "fact", brink } }));
			} else if (a_p.startHealthPct < a_b.nearDeathPct) {
				out.push_back(F("personal.already_low", { { "name", n } }));
			} else if (a_p.minHealthPct < 0.4f) {
				out.push_back(F("personal.hurt_badly", { { "name", n } }));
			} else {
				out.push_back(F("personal.no_danger", { { "name", n } }));
			}

			const auto rescue = FindRescue(a_b, a_p, a_opt);
			for (const auto& d : rescue.deeds) out.push_back(d);

			// healing not already told as part of a rescue
			for (const auto& [healer, h] : a_p.healFrom) {
				const bool told = healer != a_p.info.id && h.whileCritical >= 0.15f * a_p.info.maxHealth && a_p.WasCritical() && !a_p.dead;
				if (told || h.amount < 0.1f * a_p.info.maxHealth) continue;
				const std::string with = h.source.empty() ? std::string() : " " + F("heal.with", { { "source", h.source } });
				const auto        restoring = HealAmount(a_p, h.amount, a_opt);
				if (healer == a_p.info.id || healer == 0) out.push_back(F("heal.self", { { "name", n }, { "with", with }, { "restoring", restoring } }));
				else out.push_back(F("heal.other", { { "healer", NameOf(a_b, healer) }, { "name", n }, { "with", with }, { "restoring", restoring } }));
			}

			if (!rescue.saviors.empty()) {
				out.push_back(F("rescue.plain", { { "saviors", JoinList(Names(a_b, rescue.saviors)) }, { "name", n } }));
			} else if (a_p.WasCritical() && !a_p.dead && rescue.deeds.empty()) {
				out.push_back(F("rescue.none", { { "name", n } }));
			}

			if (const auto old = Injuries(a_b, a_p, true); !old.empty()) out.push_back(F("injury.carried", { { "name", n }, { "injuries", old } }));
			if (const auto hurt = Injuries(a_b, a_p, false); !hurt.empty() && !a_p.dead) {
				out.push_back(F("injury.new", { { "name", n }, { "injuries", hurt } }));
			}
			if (const auto bad = Effects(a_b, a_p, true, a_opt.maxEffects); !bad.empty()) out.push_back(F("effects.suffered", { { "name", n }, { "effects", bad } }));
			if (!a_opt.ignoreFriendlyFire) {
				// one line for all of it: the blows, then the magic
				std::vector<ActorId> by;
				std::string          own;
				if (FriendlyDamage(a_b, a_p, &by) >= 0.1f * a_p.info.maxHealth && !by.empty()) own = F("friendly.damage", { { "names", JoinList(Names(a_b, by)) } });
				if (const auto magic = FriendlyFire(a_b, a_p, a_opt.maxEffects); !magic.empty()) own += (own.empty() ? "" : "; ") + magic;
				// a bystander was on nobody's side, and an ally fights beside the party, not in it: not "their own side's attacks"
				const Side mine = a_b.EffectiveSide(a_p);
				// ...nor is what an ally's blow or spell did to one of the party (Durak's spell weakening Dave's armor)
				const auto notMine = [&](ActorId id) { const auto* q = a_b.Find(id); return q && a_b.EffectiveSide(*q) != mine; };
				const bool apart = Bystander(a_b, a_p) || mine == Side::kAlly || std::ranges::any_of(by, notMine) ||
				                   std::ranges::any_of(a_p.effects, [&](const EffectNote& e) { return e.hostile && OwnSide(a_b, a_p, e.by) && notMine(e.by); });
				if (!own.empty()) out.push_back(F(apart ? "friendly.crossfire_line" : "friendly.line", { { "name", n }, { "what", own } }));
			}
			{
				int cnt = 0;
				if (const auto items = ItemList(a_p.potions, &cnt); cnt > 0) out.push_back(F(cnt == 1 ? "items.potion" : "items.potions", { { "name", n }, { "count", N(cnt) }, { "items", items } }));
				if (const auto items = ItemList(a_p.poisons, &cnt); cnt > 0) out.push_back(F(cnt == 1 ? "items.poison" : "items.poisons", { { "name", n }, { "count", N(cnt) }, { "items", items } }));
			}
			if (const auto good = Effects(a_b, a_p, false, a_opt.maxEffects); !good.empty()) out.push_back(F("effects.helped", { { "name", n }, { "effects", good } }));

			std::vector<const Participant*> victims, summons;
			int                             bySummons = 0;
			const int                       mine = EnemyKills(a_b, a_p, foes, &victims, &bySummons, &summons);
			const int                       all = FoesProper(a_b, foes, true);
			if (mine > 0) {
				out.push_back(F("kills.mine", { { "name", n }, { "count", N(mine) }, { "total", N(all) }, { "victims", mine <= 3 ? ": " + Grouped(victims) : std::string() } }));
				if (bySummons > 0) out.push_back(F("kills.through", { { "count", N(bySummons) }, { "summons", Grouped(summons) } }));
			}
			else if (all > 0) out.push_back(F("kills.none", { { "name", n }, { "total", N(all) } }));

			{
				std::vector<const Participant*> dispatched;
				if (EnemyKills(a_b, a_p, foes, &dispatched, nullptr, nullptr, true) > 0) out.push_back(F("kills.dispatched", { { "name", n }, { "victims", Grouped(dispatched) } }));
			}

			// the lives this participant saved
			for (const auto& q : a_b.participants) {
				if (q.info.id == a_p.info.id) continue;
				const auto r = FindRescue(a_b, q, a_opt);
				if (std::ranges::find(r.saviors, a_p.info.id) != r.saviors.end()) {
					std::string deeds;
					for (const auto& d : r.deeds) deeds += " " + d;
					out.push_back(F("rescue.saved_other", { { "name", n }, { "other", NameOf(a_b, q.info.id) }, { "deeds", deeds } }));
				}
			}
			return out;
		}

		std::string OtherLine(const Battle& a_b, const Participant& a_p, const NarrativeOptions& a_opt)
		{
			const Side foes = Opposing(a_b.EffectiveSide(a_p));
			std::vector<std::string> bits{ Wounds(a_b, a_p, a_opt), Dealt(a_b, a_p, a_opt) };
			if (a_p.dead) bits.push_back(KilledBy(a_b, a_p.killer));
			else if (const auto brink = Brink(a_b, a_p, a_opt); !brink.empty()) {
				bits.push_back(brink);
				const auto r = FindRescue(a_b, a_p, a_opt);
				if (!r.saviors.empty()) bits.push_back(F("other.saved_by", { { "saviors", JoinList(Names(a_b, r.saviors)) } }));
			} else if (a_p.startHealthPct < a_b.nearDeathPct) {
				bits.push_back(T("other.already_low"));
			} else bits.push_back(T("other.no_danger"));
			if (const auto hurt = Injuries(a_b, a_p, false, false); !hurt.empty() && !a_p.dead) bits.push_back(F("other.injured", { { "injuries", hurt } }));
			ItemBits(a_p, bits);
			const int kills = EnemyKills(a_b, a_p, foes);
			bits.push_back(kills == 0 ? T("other.kills_none") : F("other.kills", { { "count", N(kills) } }));
			std::vector<const Participant*> dispatched;
			if (EnemyKills(a_b, a_p, foes, &dispatched, nullptr, nullptr, true) > 0) bits.push_back(F("other.dispatched", { { "victims", Grouped(dispatched) } }));
			std::string facts;
			for (std::size_t i = 0; i < bits.size(); ++i) facts += (i ? "; " : "") + bits[i];
			return F("other.line", { { "name", NameOf(a_b, a_p.info.id) }, { "facts", facts } });
		}

		// nobody on this side fell, went down, came near death or took real wounds
		bool CameThroughEasily(const Battle& a_b, Side a_side)
		{
			for (const auto* p : OnSide(a_b, a_side)) {
				if (p->dead || p->WasCritical() || p->damageTaken >= 0.25f * p->info.maxHealth || p->startHealthPct < a_b.nearDeathPct) return false;
				if (std::ranges::any_of(p->injuries, [](const InjuryNote& i) { return !i.carried; })) return false;
			}
			return true;
		}

		// Everyone whose part of the party's total is over the line (Line), largest first; at least the largest one when
		// there is anything at all.
		std::vector<const Participant*> OverTheLine(const std::vector<std::pair<float, const Participant*>>& a_parts, float* a_total = nullptr)
		{
			float total = 0.0f;
			for (const auto& [v, p] : a_parts) total += v;
			if (a_total) *a_total = total;
			auto sorted = a_parts;
			std::ranges::stable_sort(sorted, [](const auto& x, const auto& y) { return x.first > y.first; });
			std::vector<const Participant*> out;
			for (const auto& [v, p] : sorted) {
				if (v <= 0.0f) break;
				if (out.empty() || v >= Line(a_parts.size()) * total) out.push_back(p);
			}
			return out;
		}

		// What each did for the party besides damage, told only where it was earned: holding the line, healing, shielding
		// the others, hindering the enemy. Plain facts from the record; nobody is given a part they did not play.
		std::vector<std::string> Roles(const Battle& a_b, const std::vector<const Participant*>& a_party)
		{
			std::vector<std::string> out;
			if (a_party.size() < 2) return out;
			std::map<const Participant*, std::string> told;  // told otherwise than by name: "Kaira's Flame Atronach"
			const auto names = [&](const std::vector<const Participant*>& a_ps) {
				std::vector<std::string> n;
				for (const auto* p : a_ps) n.push_back(told.contains(p) ? told[p] : NameOf(a_b, p->info.id));
				return Names(n);
			};
			// the one other member a_p's help went to, when it went to only one
			const auto onlyOther = [&](const Participant* a_p, auto&& a_got) -> std::string {
				const Participant* one = nullptr;
				for (const auto* q : a_party) {
					if (q == a_p || !a_got(*q)) continue;
					if (one) return {};
					one = q;
				}
				return one ? NameOf(a_b, one->info.id) : std::string();
			};
			const auto inParty = [&](ActorId a_id) { return std::ranges::any_of(a_party, [&](const Participant* p) { return p->info.id == a_id; }); };

			// The enemy's blows: a real tank took over half of what the party took, and well over an even share (in a duo,
			// three parts in four); the others over the line took a share. Nobody standing out: they fell evenly. A summon's
			// blows are its master's part, told as the summon's when it took more of them than its master did.
			{
				const auto fromFoes = [&](const Participant& a_p) {
					float sum = 0.0f;
					for (const auto& [by, dmg] : a_p.damageFrom) {
						const auto* q = a_b.Find(by);
						if (q && a_b.EffectiveSide(*q) == Opposing(a_b.EffectiveSide(a_p))) sum += dmg;
					}
					return sum;
				};
				std::vector<std::pair<float, const Participant*>> taken;
				float                                             health = 0.0f;
				for (const auto* p : a_party) {
					const float                                       own = fromFoes(*p);
					float                                             pets = 0.0f;
					std::vector<std::pair<float, const Participant*>> mine;
					for (const auto& q : a_b.participants) {
						if (!q.info.summon || q.info.master != p->info.id || !a_b.Involved(q)) continue;
						if (const float d = fromFoes(q); d > 0.0f) {
							pets += d;
							mine.emplace_back(d, &q);
						}
					}
					taken.emplace_back(own + pets, p);
					health += p->info.maxHealth;
					if (pets > own) {
						// one by its name; several (an atronach and a raised corpse) together, so a list of names stays a list
						if (mine.size() == 1) told[p] = F("name.summons_of", { { "master", NameOf(a_b, p->info.id) }, { "summons", mine.front().second->info.name } });
						else told[p] = F("name.summons_many", { { "master", NameOf(a_b, p->info.id) } });
					}
				}
				float total = 0.0f;
				auto  over = OverTheLine(taken, &total);
				// the enemy never touched them (a clean ambush, a fight won from range): said so, and no share of anything
				if (total <= 0.0f && a_b.Count(Side::kEnemy) > 0) out.push_back(T("role.front_none"));
				// worth telling once the party, between them, lost a real part of one body's health
				if (!over.empty() && total >= 0.25f * health / static_cast<float>(a_party.size())) {
					float top = 0.0f;
					for (const auto& [v, p] : taken) {
						if (p == over.front()) top = v;
					}
					const float even = EvenShare(a_party.size());
					if (top > std::max(0.5f, 1.5f * even) * total) {
						out.push_back(F("role.front_brunt", { { "names", names({ over.front() }) } }));
						over.erase(over.begin());
						if (!over.empty()) out.push_back(F("role.front_also", { { "names", names(over) } }));
					} else if (top < kStandOut * even * total) out.push_back(T("role.front_even"));
					else if (over.size() >= 2) out.push_back(F("role.front_shared", { { "names", names(over) } }));
					else out.push_back(F("role.front_most", { { "names", names(over) } }));
				}
			}
			// healing given to others
			{
				std::vector<std::pair<float, const Participant*>> given;
				for (const auto* p : a_party) {
					float sum = 0.0f;
					for (const auto& q : a_b.participants) {
						if (q.info.id == p->info.id || !inParty(q.info.id)) continue;
						if (const auto it = q.healFrom.find(p->info.id); it != q.healFrom.end()) sum += it->second.amount;
					}
					given.emplace_back(sum, p);
				}
				float      total = 0.0f;
				const auto over = OverTheLine(given, &total);
				if (!over.empty() && total >= 0.15f * a_party.front()->info.maxHealth) {
					const auto other = over.size() == 1 ? onlyOther(over.front(), [&](const Participant& q) { return q.healFrom.contains(over.front()->info.id); }) : std::string();
					if (!other.empty()) out.push_back(F("role.healer_one", { { "names", names(over) }, { "other", other } }));
					else out.push_back(F("role.healer", { { "names", names(over) } }));
				}
			}
			// help cast on others, and harm that hindered the enemy, by how often it took hold
			{
				const std::set<std::string> hinder{ T("fx.paralyzed"), T("fx.fear"), T("fx.frenzy"), T("fx.calm"), T("fx.staggered"), T("fx.disarmed"), T("fx.slowed"), T("fx.turned") };
				std::vector<std::pair<float, const Participant*>> shielded, hindered;
				std::vector<std::string>                          kinds;
				for (const auto* p : a_party) {
					float help = 0.0f, held = 0.0f;
					for (const auto& q : a_b.participants) {
						if (q.info.id == p->info.id) continue;
						const bool friend_ = inParty(q.info.id);
						const bool foe = a_b.Involved(q) && a_b.EffectiveSide(q) == Opposing(a_b.EffectiveSide(*p));
						for (const auto& e : q.effects) {
							if (e.by != p->info.id) continue;
							if (!e.hostile && friend_) help += static_cast<float>(e.count);
							if (e.hostile && foe && hinder.contains(e.label) && a_b.Landed(q, e)) {
								held += static_cast<float>(e.count);
								if (std::ranges::find(kinds, e.label) == kinds.end()) kinds.push_back(e.label);
							}
						}
					}
					shielded.emplace_back(help, p);
					hindered.emplace_back(held, p);
				}
				float total = 0.0f;
				if (const auto over = OverTheLine(shielded, &total); !over.empty() && total >= 2.0f) {
					const auto helped = [&](const Participant& q) {
						return std::ranges::any_of(q.effects, [&](const EffectNote& e) { return e.by == over.front()->info.id && !e.hostile; });
					};
					const auto other = over.size() == 1 ? onlyOther(over.front(), helped) : std::string();
					if (!other.empty()) out.push_back(F("role.protector_one", { { "names", names(over) }, { "other", other } }));
					else out.push_back(F("role.protector", { { "names", names(over) } }));
				}
				if (const auto over = OverTheLine(hindered, &total); !over.empty() && total >= 2.0f) {
					out.push_back(F("role.controller", { { "names", names(over) }, { "effects", JoinList(kinds) } }));
				}
			}
			return out;
		}

		std::vector<std::string> General(const Battle& a_b, const NarrativeOptions& a_opt)
		{
			std::vector<std::string> out;
			const auto               party = OnSide(a_b, Side::kPlayer), enemies = OnSide(a_b, Side::kEnemy), others = OnSide(a_b, Side::kOther),
			                         allies = OnSide(a_b, Side::kAlly);
			const std::string        where = a_b.location.empty() ? std::string() : " " + F("battle.at", { { "place", a_b.location } });
			if (a_b.Ongoing()) out.push_back(F("battle.underway", { { "at_place", where } }));
			else if (a_b.clashes > 1) {
				out.push_back(F("battle.fought_clashes", { { "at_place", where }, { "count", N(a_b.clashes) }, { "duration", DurationText(a_b.endedAt - a_b.startedAt) } }));
			} else out.push_back(F("battle.fought", { { "at_place", where }, { "duration", DurationText(a_b.endedAt - a_b.startedAt) } }));

			if (!party.empty() && !enemies.empty()) out.push_back(F("battle.sides", { { "party", Grouped(party) }, { "enemies", Grouped(enemies) } }));
			else if (!enemies.empty()) out.push_back(F("battle.enemies_only", { { "enemies", Grouped(enemies) } }));
			if (!allies.empty() && !enemies.empty()) out.push_back(F("battle.allies", { { "allies", Grouped(allies) } }));
			if (!others.empty()) out.push_back(F("battle.caught_up", { { "others", Grouped(others) } }));

			if (auto o = Outcome(a_b, Side::kEnemy); !o.empty()) out.push_back(std::move(o));
			if (const auto t = KillTally(a_b, Side::kEnemy); !t.empty()) out.push_back(F("tally.line", { { "tally", t } }));
			// A kill goes to the last blow; the damage is the fight. Those who stood out (well over an even share), up to three,
			// until they make up most of it: a follower who wore the dragon down is named though the player struck the last
			// blow. "Most of" only when they did over half; nobody standing out, an even fight is said plainly.
			if (party.size() >= 2) {
				const float total = SideDealtToFoes(a_b, Side::kPlayer);
				std::vector<std::pair<float, const Participant*>> shares;
				for (const auto* p : party) {
					if (const float d = PartyDealt(a_b, *p); d > 0.0f) shares.emplace_back(d, p);
				}
				std::ranges::stable_sort(shares, [](const auto& x, const auto& y) { return x.first > y.first; });
				const float even = EvenShare(party.size());
				if (total > 0.0f && !shares.empty()) {
					if (shares.front().first < kStandOut * even * total) out.push_back(T("battle.damage_even"));
					else {
						std::vector<std::string> names;
						float                    covered = 0.0f;
						std::size_t              i = 0;
						for (; i < shares.size(); ++i) {
							const auto& [d, p] = shares[i];
							if (names.size() >= 3 || covered >= 0.6f * total || d < kStandOut * even * total) break;
							covered += d;
							std::string name = NameOf(a_b, p->info.id);
							if (a_opt.showNumbers) name += " " + F("share.percent", { { "percent", N(static_cast<int>(std::lround(100.0f * d / total))) } });
							names.push_back(name);
						}
						out.push_back(F(covered > 0.5f * total ? "battle.damage_by" : "battle.damage_led", { { "names", JoinList(names) } }));
						// the others who did a real part of it
						std::vector<std::string> also;
						for (; i < shares.size(); ++i) {
							if (shares[i].first >= Line(party.size()) * total) also.push_back(NameOf(a_b, shares[i].second->info.id));
						}
						if (!also.empty()) out.push_back(F("battle.damage_also", { { "names", Names(also) } }));
					}
				}
			}
			for (auto& l : Roles(a_b, party)) out.push_back(std::move(l));
			if (const auto t = KillTally(a_b, Side::kEnemy, true); !t.empty()) out.push_back(F("summons.dispatched", { { "tally", t } }));
			for (const auto* p : party) {
				if (!p->dead) continue;
				// killed by their own side: said outright, accident or not
				const auto* k = a_b.Find(p->killer);
				const bool  ownSide = k && k->info.id != p->info.id && Friends(a_b.EffectiveSide(*k), Side::kPlayer);
				out.push_back(F("killed.line", { { "name", NameOf(a_b, p->info.id) }, { "killed", KilledBy(a_b, p->killer) }, { "own_side", ownSide ? ", " + T("killed.own_side") : std::string() } }));
			}
			for (const auto* p : allies) {
				if (!p->dead) continue;
				const auto* k = a_b.Find(p->killer);
				const bool  ownSide = k && k->info.id != p->info.id && Friends(a_b.EffectiveSide(*k), Side::kAlly);
				out.push_back(F("killed.line", { { "name", NameOf(a_b, p->info.id) }, { "killed", KilledBy(a_b, p->killer) }, { "own_side", ownSide ? ", " + T("killed.own_side") : std::string() } }));
			}
			for (const auto* p : enemies) {
				if (!p->dead || !OwnSideKill(a_b, *p)) continue;
				out.push_back(F("killed.line", { { "name", NameOf(a_b, p->info.id) }, { "killed", KilledBy(a_b, p->killer) }, { "own_side", ", " + T("killed.own_side") } }));
			}
			for (const auto* p : others) {
				if (a_b.omitBystanders || !p->dead || !a_b.Innocent(*p)) continue;
				out.push_back(F("killed.not_fighting", { { "name", NameOf(a_b, p->info.id) }, { "killed", KilledBy(a_b, p->killer) } }));
			}
			for (auto& m : KilledAtOnce(a_b)) out.push_back(std::move(m));
			for (auto& m : Maimings(a_b)) out.push_back(std::move(m));
			return out;
		}
	}

	Summary BuildSummary(const Battle& a_b, ActorId a_viewer, const std::string& a_viewerName, const NarrativeOptions& a_opt)
	{
		Summary r;
		if (a_b.participants.empty()) return r;
		r.show = true;
		r.ongoing = a_b.Ongoing();
		r.name = a_viewerName;
		r.general = General(a_b, a_opt);
		for (auto& l : r.general) l = Cap(std::move(l));

		const auto* me = a_b.Find(a_viewer);
		if (me && !a_b.Involved(*me)) me = nullptr;  // in the game's combat state, but never fought: a witness
		r.participant = me != nullptr;
		r.ally = me && a_b.EffectiveSide(*me) == Side::kPlayer;
		if (me) {
			if (r.name.empty()) r.name = me->info.name;
			r.personal = Personal(a_b, *me, a_opt);
			for (auto& l : r.personal) l = Cap(std::move(l));
		}
		// their companions: the viewer's own side, or the player's side for a witness
		const Side side = me && a_b.EffectiveSide(*me) == Side::kEnemy ? Side::kEnemy : Side::kPlayer;
		// an easy fight needs no roll call
		if (CameThroughEasily(a_b, side)) {
			if (OnSide(a_b, side).size() > (me ? 1u : 0u)) r.others.push_back(T("other.easy"));
			return r;
		}
		for (const auto* p : OnSide(a_b, side)) {
			if (p->info.id == a_viewer) continue;
			if (static_cast<int>(r.others.size()) >= a_opt.maxOthers) break;
			r.others.push_back(Cap(OtherLine(a_b, *p, a_opt)));
		}
		return r;
	}

	std::string BuildMemory(const Battle& a_b, const NarrativeOptions& a_opt)
	{
		std::string s;
		const auto  add = [&](const std::string& a_line) {
			if (a_line.empty()) return;
			if (!s.empty()) s += ' ';
			s += Cap(a_line);
		};
		for (const auto& l : General(a_b, a_opt)) add(l);
		for (const auto* p : OnSide(a_b, Side::kPlayer)) {
			if (p->dead) continue;
			if (const auto hurt = Injuries(a_b, *p, false); !hurt.empty()) add(F("injury.memory", { { "name", p->info.name }, { "injuries", hurt } }));
			if (!p->WasCritical()) continue;
			const auto        r = FindRescue(a_b, *p, a_opt);
			const std::string brink = Brink(a_b, *p, a_opt, r.saviors.empty());
			if (!r.saviors.empty()) {
				add(F("memory.rescued", { { "name", p->info.name }, { "brink", brink }, { "deeds", JoinList(r.brief) }, { "saviors", JoinList(Names(a_b, r.saviors)) } }));
			} else {
				add(F("memory.survived", { { "name", p->info.name }, { "brink", brink } }));
			}
		}
		return s;
	}

	std::string DurationText(double a_seconds)
	{
		if (a_seconds < 60.0) {
			const int s = std::max(1, static_cast<int>(std::lround(a_seconds)));
			if (s < 60) return s == 1 ? T("duration.second") : F("duration.seconds", { { "count", N(s) } });
		}
		const double minutes = a_seconds / 60.0;
		if (minutes < 5.0) {
			const int m = std::max(1, static_cast<int>(std::lround(minutes)));
			if (m < 5) return m == 1 ? T("duration.minute") : F("duration.minutes", { { "count", N(m) } });
		}
		if (minutes <= 60.0) return F("duration.minutes", { { "count", N(std::max(5, static_cast<int>(std::lround(minutes / 5.0)) * 5)) } });
		const int halves = std::max(2, static_cast<int>(std::lround(minutes / 30.0)));  // half hours
		if (halves == 2) return T("duration.hour");
		return F(halves % 2 == 0 ? "duration.hours" : "duration.hours_half", { { "count", N(halves / 2) } });
	}

	std::string SummaryText(const Summary& a_r)
	{
		if (!a_r.show) return "(no battle to recall)";
		std::string s = a_r.ongoing ? "BATTLE UNDERWAY\n" : "BATTLE JUST FOUGHT\n";
		for (const auto& l : a_r.general) s += "- " + l + "\n";
		if (!a_r.personal.empty()) {
			s += std::format("What happened to {}:\n", a_r.name);
			for (const auto& l : a_r.personal) s += "- " + l + "\n";
		}
		if (!a_r.others.empty()) {
			s += "The others:\n";
			for (const auto& l : a_r.others) s += "- " + l + "\n";
		}
		return s;
	}

	bool Significant(const Battle& a_b, int a_minEnemies, double a_minSeconds)
	{
		for (const auto& p : a_b.participants) {
			if (!a_b.omitBystanders && p.dead && a_b.Innocent(p)) return true;  // someone was killed without fighting
		}
		if (a_b.Count(Side::kPlayer) == 0 || a_b.Count(Side::kEnemy) == 0) return false;  // nothing the player's side fought
		for (const auto* p : OnSide(a_b, Side::kPlayer)) {
			if (p->dead || p->WasCritical()) return true;
			if (std::ranges::any_of(p->injuries, [](const InjuryNote& i) { return !i.carried; })) return true;
		}
		if (a_b.Count(Side::kEnemy) >= a_minEnemies) return true;
		return !a_b.Ongoing() && a_b.endedAt - a_b.startedAt >= a_minSeconds;
	}
}
