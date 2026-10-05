#include "Narrative.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace BSM
{
	namespace
	{
		// "a", "a and b", "a, b and c"
		std::string JoinList(const std::vector<std::string>& a_items)
		{
			std::string out;
			for (std::size_t i = 0; i < a_items.size(); ++i) {
				if (i > 0) out += (i + 1 == a_items.size()) ? " and " : ", ";
				out += a_items[i];
			}
			return out;
		}

		std::string NameOf(const Battle& a_b, ActorId a_id)
		{
			if (a_id == 0) return "no one in particular";
			const auto* p = a_b.Find(a_id);
			return p && !p->info.name.empty() ? p->info.name : "someone";
		}

		// for kill and deed credit: a summon is named with its master
		std::string CreditName(const Battle& a_b, ActorId a_id)
		{
			const auto* p = a_b.Find(a_id);
			if (p && p->info.master != 0 && a_b.Find(p->info.master)) {
				return std::format("{} (commanded by {})", NameOf(a_b, a_id), NameOf(a_b, p->info.master));
			}
			return NameOf(a_b, a_id);
		}

		// "Bandit x3, Bandit Chief": same-named actors are counted, in first-seen order; past a_max names, "and N others"
		std::string Grouped(const std::vector<const Participant*>& a_ps, std::size_t a_max = 4)
		{
			std::vector<std::pair<std::string, int>> groups;
			for (const auto* p : a_ps) {
				const auto& n = p->info.name.empty() ? std::string("someone") : p->info.name;
				const auto  it = std::ranges::find_if(groups, [&](const auto& g) { return g.first == n; });
				if (it == groups.end()) groups.emplace_back(n, 1);
				else ++it->second;
			}
			std::vector<std::string> items;
			int                      rest = 0;
			for (const auto& [n, c] : groups) {
				// one name over the limit is shorter told than "and 1 other"
				if (items.size() < a_max || groups.size() == a_max + 1) items.push_back(c > 1 ? std::format("{} x{}", n, c) : n);
				else rest += c;
			}
			if (rest > 0) items.push_back(std::format("{} others", rest));
			return JoinList(items);
		}

		std::vector<const Participant*> OnSide(const Battle& a_b, Side a_side)
		{
			std::vector<const Participant*> out;
			for (const auto& p : a_b.participants) {
				if (a_b.Involved(p) && a_b.EffectiveSide(p) == a_side) out.push_back(&p);
			}
			return out;
		}

		Side Opposing(Side a_side) { return a_side == Side::kEnemy ? Side::kPlayer : Side::kEnemy; }

		std::string Points(float a_amount, const NarrativeOptions& a_opt)
		{
			return a_opt.showNumbers ? std::format(" (about {} points)", static_cast<int>(std::lround(a_amount))) : std::string();
		}

		std::string Duration(const Battle& a_b)
		{
			if (a_b.Ongoing()) return {};
			const double s = a_b.endedAt - a_b.startedAt;
			if (s < 20.0) return "over in moments";
			if (s < 50.0) return "a short fight of well under a minute";
			if (s < 100.0) return "a fight of about a minute";
			return std::format("a long fight of about {} minutes", static_cast<int>(std::lround(s / 60.0)));
		}

		std::string Wounds(const Participant& a_p, const NarrativeOptions& a_opt)
		{
			const float r = a_p.info.maxHealth > 0.0f ? a_p.damageTaken / a_p.info.maxHealth : 0.0f;
			std::string w;
			if (r < 0.03f) w = "took no damage worth the name";
			else if (r < 0.25f) w = "took only light wounds";
			else if (r < 0.6f) w = "took solid wounds";
			else if (r < 1.0f) w = "was badly hurt, taking nearly a full body's worth of damage";
			else if (a_p.healingReceived > 0.0f) w = "took more damage than a body can survive in one go, and only kept going thanks to healing";
			else w = "took a brutal amount of damage";
			return w + Points(a_p.damageTaken, a_opt);
		}

		std::string Dealt(const Battle& a_b, const Participant& a_p, const NarrativeOptions& a_opt)
		{
			const Side  side = a_b.EffectiveSide(a_p);
			const float total = a_b.SideDamageDealt(side);
			if (a_p.damageDealt <= 0.0f) return "dealt no damage to anyone";
			std::string w;
			if (a_b.Count(side) <= 1 || total <= 0.0f) w = "dealt all the damage on their side";
			else {
				const float s = a_p.damageDealt / total;
				if (s < 0.15f) w = "dealt only a small part of their side's damage";
				else if (s < 0.4f) w = "dealt a fair share of their side's damage";
				else if (s < 0.7f) w = "dealt a large share of their side's damage";
				else w = "dealt most of their side's damage";
			}
			return w + Points(a_p.damageDealt, a_opt);
		}

		std::string HealAmount(const Participant& a_p, float a_amount, const NarrativeOptions& a_opt)
		{
			const float r = a_p.info.maxHealth > 0.0f ? a_amount / a_p.info.maxHealth : 0.0f;
			const char* w = r < 0.3f ? "a little of" : r < 0.7f ? "a good part of" : "most or all of";
			return std::format("restoring {} {}'s health{}", w, a_p.info.name, Points(a_amount, a_opt));
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
			if (!a_p.WasCritical() || a_p.dead) return r;
			const auto addSavior = [&](ActorId a_id) {
				if (a_id != 0 && a_id != a_p.info.id && std::ranges::find(r.saviors, a_id) == r.saviors.end()) r.saviors.push_back(a_id);
			};
			const Side  mine = a_b.EffectiveSide(a_p);
			const char* what = a_p.downs > 0 ? "struck down" : "nearly killed";

			// the assailants: whoever dealt the blows that did it, and anyone who dealt a large part of all the damage
			std::vector<ActorId> assailants;
			if (a_p.broughtLowBy != 0) assailants.push_back(a_p.broughtLowBy);
			for (const auto& [id, dmg] : a_p.damageFrom) {
				if (id != 0 && dmg >= 0.4f * a_p.damageTaken && std::ranges::find(assailants, id) == assailants.end()) assailants.push_back(id);
			}
			for (const auto id : assailants) {
				const auto* a = a_b.Find(id);
				if (!a || !a->dead || a->diedAt < a_p.criticalAt || a_b.EffectiveSide(*a) == mine) continue;
				if (a->killer == a_p.info.id) {
					r.deeds.push_back(std::format("{} recovered enough to kill {}, who had {} {}.", a_p.info.name, a->info.name, what, a_p.info.name));
				} else if (a->killer != 0) {
					r.deeds.push_back(std::format("{}, who had {} {}, was then killed by {}.", a->info.name, what, a_p.info.name, CreditName(a_b, a->killer)));
					r.brief.push_back(std::format("{} killed {}", NameOf(a_b, a->killer), a->info.name));
					const auto* k = a_b.Find(a->killer);
					addSavior(k && k->info.master != 0 && a_b.Find(k->info.master) ? k->info.master : a->killer);
				}
			}
			for (const auto& [healer, h] : a_p.healFrom) {
				if (healer == 0 || healer == a_p.info.id || h.whileCritical < 0.15f * a_p.info.maxHealth) continue;
				r.deeds.push_back(std::format("While {} was {}, {} healed {}{}, {}.", a_p.info.name, a_p.downs > 0 ? "down" : "at death's door",
					NameOf(a_b, healer), a_p.info.name, h.source.empty() ? std::string() : std::format(" with {}", h.source), HealAmount(a_p, h.whileCritical, a_opt)));
				r.brief.push_back(std::format("{} healed {}", NameOf(a_b, healer), a_p.info.name));
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

		// "was struck down (collapsed, bleeding out) twice by X" / "was brought to the edge of death by X" / ""
		std::string Brink(const Battle& a_b, const Participant& a_p)
		{
			const std::string by = a_p.broughtLowBy != 0 ? std::format(" by {}", NameOf(a_b, a_p.broughtLowBy)) : std::string();
			if (a_p.downs > 0) {
				const std::string times = a_p.downs == 1 ? "" : a_p.downs == 2 ? " twice" : std::format(" {} times", a_p.downs);
				return std::format("was struck down{}{}: collapsed, helpless and bleeding out, unable to fight", times, by);
			}
			if (a_p.nearDeath) return std::format("was brought to the very edge of death{}, with almost no health left", by);
			return {};
		}

		// enemy-of-a_side kills per killer, most first: "Kaira 3 (Bandit x2, Bandit Chief), Lydia 1 (Bandit)"
		std::string KillTally(const Battle& a_b, Side a_victims)
		{
			std::vector<std::pair<ActorId, std::vector<const Participant*>>> tally;
			for (const auto* v : OnSide(a_b, a_victims)) {
				if (!v->dead) continue;
				const auto it = std::ranges::find_if(tally, [&](const auto& t) { return t.first == v->killer; });
				if (it == tally.end()) tally.push_back({ v->killer, { v } });
				else it->second.push_back(v);
			}
			std::ranges::stable_sort(tally, [](const auto& a, const auto& b) { return a.second.size() > b.second.size(); });
			std::vector<std::string> items;
			for (const auto& [killer, victims] : tally) {
				// the victims by name only while the list stays short
				items.push_back(std::format("{} {}{}", killer == 0 ? std::string("unknown causes") : CreditName(a_b, killer), victims.size(),
					victims.size() <= 3 ? std::format(" ({})", Grouped(victims)) : std::string()));
			}
			std::string out;
			for (const auto& i : items) out += (out.empty() ? "" : "; ") + i;
			return out;
		}

		std::string Outcome(const Battle& a_b, Side a_foes)
		{
			const int n = a_b.Count(a_foes), dead = a_b.Dead(a_foes);
			const char* word = a_foes == Side::kEnemy ? "enemies" : "opponents";
			if (n == 0) return {};
			if (a_b.Ongoing()) return std::format("So far {} of the {} {} are dead.", dead, n, word);
			if (dead == n) return n == 1 ? std::string("The one enemy was killed.") : std::format("All {} {} were killed.", n, word);
			if (dead == 0) return std::format("None of the {} {} were killed; the fight broke off.", n, word);
			return std::format("{} of the {} {} were killed; the rest fled, yielded or broke off.", dead, n, word);
		}

		std::string EffectPhrase(const Battle& a_b, const Participant& a_p, const EffectNote& a_e)
		{
			std::string s = a_e.label;
			if (a_e.by != 0 && a_e.by != a_p.info.id) {
				s += std::format(" by {}", NameOf(a_b, a_e.by));
				if (!a_e.source.empty()) s += std::format(" ({})", a_e.source);
			} else if (!a_e.source.empty()) {
				s += a_e.by == 0 ? std::format(" ({})", a_e.source) : std::format(" (own {})", a_e.source);  // by nobody: a trap, a hazard
			}
			return s;
		}

		std::string Effects(const Battle& a_b, const Participant& a_p, bool a_hostile, int a_max)
		{
			std::vector<std::string> items;
			int                      more = 0;
			for (const auto& e : a_p.effects) {
				if (e.hostile != a_hostile) continue;
				if (static_cast<int>(items.size()) < a_max) items.push_back(EffectPhrase(a_b, a_p, e));
				else ++more;
			}
			if (items.empty()) return {};
			auto s = JoinList(items);
			if (more > 0) s += std::format(", and {} more", more);
			return s;
		}

		int EnemyKills(const Battle& a_b, const Participant& a_p, Side a_foes, std::vector<const Participant*>* a_out = nullptr)
		{
			int n = 0;
			for (const auto id : a_p.kills) {
				const auto* v = a_b.Find(id);
				if (v && a_b.EffectiveSide(*v) == a_foes) {
					++n;
					if (a_out) a_out->push_back(v);
				}
			}
			return n;
		}

		std::vector<std::string> Personal(const Battle& a_b, const Participant& a_p, const NarrativeOptions& a_opt)
		{
			std::vector<std::string> out;
			const auto&              n = a_p.info.name;
			const Side               foes = Opposing(a_b.EffectiveSide(a_p));

			out.push_back(std::format("{} {} and {}.", n, Wounds(a_p, a_opt), Dealt(a_b, a_p, a_opt)));

			if (a_p.dead) {
				out.push_back(std::format("{} was killed by {}.", n, CreditName(a_b, a_p.killer)));
			} else if (const auto brink = Brink(a_b, a_p); !brink.empty()) {
				out.push_back(std::format("{} {}.", n, brink));
			} else if (a_p.startHealthPct < a_b.nearDeathPct) {
				out.push_back(std::format("{} went into this fight already close to death from earlier wounds, and came through it without being hurt much further.", n));
			} else if (a_p.minHealthPct < 0.4f) {
				out.push_back(std::format("{} was hurt badly at the worst of it, but was never at death's door.", n));
			} else {
				out.push_back(std::format("{} was never in serious danger.", n));
			}

			const auto rescue = FindRescue(a_b, a_p, a_opt);
			for (const auto& d : rescue.deeds) out.push_back(d);

			// healing not already told as part of a rescue
			for (const auto& [healer, h] : a_p.healFrom) {
				const bool told = healer != a_p.info.id && h.whileCritical >= 0.15f * a_p.info.maxHealth && a_p.WasCritical() && !a_p.dead;
				if (told || h.amount < 0.1f * a_p.info.maxHealth) continue;
				const std::string with = h.source.empty() ? std::string() : std::format(" with {}", h.source);
				if (healer == a_p.info.id || healer == 0) out.push_back(std::format("{} healed themself{}, {}.", n, with, HealAmount(a_p, h.amount, a_opt)));
				else out.push_back(std::format("{} healed {}{}, {}.", NameOf(a_b, healer), n, with, HealAmount(a_p, h.amount, a_opt)));
			}

			if (!rescue.saviors.empty()) {
				out.push_back(std::format("In plain terms: {} saved {}'s life in this battle. Without that help {} would most likely have died.",
					JoinList(Names(a_b, rescue.saviors)), n, n));
			} else if (a_p.WasCritical() && !a_p.dead && rescue.deeds.empty()) {
				out.push_back(std::format("{} survived that without anyone's rescue.", n));
			}

			if (const auto bad = Effects(a_b, a_p, true, a_opt.maxEffects); !bad.empty()) out.push_back(std::format("{} suffered: {}.", n, bad));
			if (const auto good = Effects(a_b, a_p, false, a_opt.maxEffects); !good.empty()) out.push_back(std::format("{} was helped by: {}.", n, good));

			std::vector<const Participant*> victims;
			const int                       mine = EnemyKills(a_b, a_p, foes, &victims);
			const int                       all = a_b.Dead(foes);
			if (mine > 0) {
				out.push_back(std::format("{} killed {} of the {} who died on the other side{}.", n, mine, all, mine <= 3 ? std::format(": {}", Grouped(victims)) : std::string()));
			}
			else if (all > 0) out.push_back(std::format("{} killed none of the {} who died on the other side.", n, all));

			// the lives this participant saved
			for (const auto& q : a_b.participants) {
				if (q.info.id == a_p.info.id) continue;
				const auto r = FindRescue(a_b, q, a_opt);
				if (std::ranges::find(r.saviors, a_p.info.id) != r.saviors.end()) {
					std::string deeds;
					for (const auto& d : r.deeds) deeds += " " + d;
					out.push_back(std::format("{} saved {}'s life.{}", n, q.info.name, deeds));
				}
			}
			return out;
		}

		std::string OtherLine(const Battle& a_b, const Participant& a_p, const NarrativeOptions& a_opt)
		{
			const Side foes = Opposing(a_b.EffectiveSide(a_p));
			std::vector<std::string> bits{ Wounds(a_p, a_opt) };
			if (a_p.dead) bits.push_back(std::format("was killed by {}", CreditName(a_b, a_p.killer)));
			else if (const auto brink = Brink(a_b, a_p); !brink.empty()) {
				bits.push_back(brink);
				const auto r = FindRescue(a_b, a_p, a_opt);
				if (!r.saviors.empty()) bits.push_back(std::format("was saved by {}", JoinList(Names(a_b, r.saviors))));
			} else if (a_p.startHealthPct < a_b.nearDeathPct) {
				bits.push_back("went in already close to death from earlier wounds");
			} else bits.push_back("was never near death");
			const int kills = EnemyKills(a_b, a_p, foes);
			bits.push_back(kills == 0 ? std::string("killed none of the other side") : std::format("killed {} of the other side", kills));
			std::string line = a_p.info.name + ": ";
			for (std::size_t i = 0; i < bits.size(); ++i) line += (i ? "; " : "") + bits[i];
			return line + ".";
		}

		// nobody on this side fell, went down, came near death or took real wounds
		bool CameThroughEasily(const Battle& a_b, Side a_side)
		{
			for (const auto* p : OnSide(a_b, a_side)) {
				if (p->dead || p->WasCritical() || p->damageTaken >= 0.25f * p->info.maxHealth || p->startHealthPct < a_b.nearDeathPct) return false;
			}
			return true;
		}

		std::vector<std::string> General(const Battle& a_b)
		{
			std::vector<std::string> out;
			const auto               party = OnSide(a_b, Side::kPlayer), enemies = OnSide(a_b, Side::kEnemy), others = OnSide(a_b, Side::kOther);
			const std::string        where = a_b.location.empty() ? std::string() : std::format(" at {}", a_b.location);
			if (a_b.Ongoing()) out.push_back(std::format("A battle is being fought{} right now.", where));
			else out.push_back(std::format("A battle was fought{}: {}.", where, Duration(a_b)));

			if (!party.empty() && !enemies.empty()) out.push_back(std::format("{} fought against {}.", Grouped(party), Grouped(enemies)));
			else if (!enemies.empty()) out.push_back(std::format("The enemies were {}.", Grouped(enemies)));
			if (!others.empty()) out.push_back(std::format("Also caught up in it: {}.", Grouped(others)));

			if (auto o = Outcome(a_b, Side::kEnemy); !o.empty()) out.push_back(std::move(o));
			if (const auto t = KillTally(a_b, Side::kEnemy); !t.empty()) out.push_back(std::format("Who killed the enemies: {}.", t));
			for (const auto* p : party) {
				if (p->dead) out.push_back(std::format("{} was killed by {}.", p->info.name, CreditName(a_b, p->killer)));
			}
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
		r.general = General(a_b);

		const auto* me = a_b.Find(a_viewer);
		r.participant = me != nullptr;
		if (me) {
			if (r.name.empty()) r.name = me->info.name;
			r.personal = Personal(a_b, *me, a_opt);
		}
		// their companions: the viewer's own side, or the player's side for a witness
		const Side side = me && a_b.EffectiveSide(*me) == Side::kEnemy ? Side::kEnemy : Side::kPlayer;
		// an easy fight needs no roll call
		if (CameThroughEasily(a_b, side)) {
			if (OnSide(a_b, side).size() > (me ? 1u : 0u)) r.others.push_back("None of them was seriously hurt or in any real danger.");
			return r;
		}
		for (const auto* p : OnSide(a_b, side)) {
			if (p->info.id == a_viewer) continue;
			if (static_cast<int>(r.others.size()) >= a_opt.maxOthers) break;
			r.others.push_back(OtherLine(a_b, *p, a_opt));
		}
		return r;
	}

	std::string BuildMemory(const Battle& a_b, const NarrativeOptions& a_opt)
	{
		std::string s;
		const auto  add = [&](const std::string& a_line) {
			if (a_line.empty()) return;
			if (!s.empty()) s += ' ';
			s += a_line;
		};
		for (const auto& l : General(a_b)) add(l);
		for (const auto* p : OnSide(a_b, Side::kPlayer)) {
			if (p->dead || !p->WasCritical()) continue;
			const auto        r = FindRescue(a_b, *p, a_opt);
			const std::string brink = Brink(a_b, *p);
			if (!r.saviors.empty()) {
				add(std::format("{} {}, and would most likely have died, but {}: {} saved {}'s life.", p->info.name, brink, JoinList(r.brief),
					JoinList(Names(a_b, r.saviors)), p->info.name));
			} else {
				add(std::format("{} {}, but survived.", p->info.name, brink));
			}
		}
		return s;
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
		if (a_b.Count(Side::kPlayer) == 0 || a_b.Count(Side::kEnemy) == 0) return false;  // nothing the player's side fought
		for (const auto* p : OnSide(a_b, Side::kPlayer)) {
			if (p->dead || p->WasCritical()) return true;
		}
		if (a_b.Count(Side::kEnemy) >= a_minEnemies) return true;
		return !a_b.Ongoing() && a_b.endedAt - a_b.startedAt >= a_minSeconds;
	}
}
