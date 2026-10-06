#include "Core.h"

#include <algorithm>

namespace BSM
{
	namespace
	{
		// how long after a blow its dealer still counts as the one who brought the victim low or killed them
		constexpr double kAttackerMemory = 8.0;
		// a death this soon after a blow is that blow's doing
		constexpr double kFinalBlow = 3.0;

		ActorId TopDamager(const Participant& a_p)
		{
			ActorId best = 0;
			float   most = 0.0f;
			for (const auto& [id, dmg] : a_p.damageFrom) {
				if (id != 0 && dmg > most) most = dmg, best = id;
			}
			return best;
		}

		ActorId RecentAttacker(const Participant& a_p, double a_now)
		{
			if (a_p.lastAttacker != 0 && a_now - a_p.lastAttackedAt <= kAttackerMemory) return a_p.lastAttacker;
			return TopDamager(a_p);
		}
	}

	Participant* Battle::Find(ActorId a_id)
	{
		for (auto& p : participants) {
			if (p.info.id == a_id) return &p;
		}
		return nullptr;
	}

	const Participant* Battle::Find(ActorId a_id) const
	{
		for (const auto& p : participants) {
			if (p.info.id == a_id) return &p;
		}
		return nullptr;
	}

	Participant& Battle::Join(const ActorInfo& a_info)
	{
		if (auto* p = Find(a_info.id)) {
			if (!a_info.name.empty()) p->info.name = a_info.name;
			if (a_info.maxHealth > 0.0f) p->info.maxHealth = a_info.maxHealth;
			if (a_info.master != 0) p->info.master = a_info.master;
			if (a_info.lawful) p->info.lawful = true;
			// a follower stays a follower through a stray friendly hit; anyone else who turns hostile is an enemy from then on
			if (p->info.side == Side::kOther || a_info.side == Side::kPlayer) p->info.side = a_info.side;
			return *p;
		}
		participants.push_back({});
		auto& p = participants.back();
		p.info = a_info;
		p.startHealthPct = p.healthPct = p.minHealthPct = std::clamp(a_info.healthPct, 0.0f, 1.0f);
		return p;
	}

	void Battle::MarkCritical(Participant& a_p, double a_now)
	{
		if (a_p.criticalAt < 0.0) {
			a_p.criticalAt = a_now;
			a_p.broughtLowBy = RecentAttacker(a_p, a_now);
		}
	}

	void Battle::Sample(ActorId a_id, float a_healthPct, double a_now)
	{
		auto* p = Find(a_id);
		if (!p || a_healthPct < 0.0f) return;
		a_healthPct = std::clamp(a_healthPct, 0.0f, 1.0f);
		p->healthPct = a_healthPct;
		p->minHealthPct = std::min(p->minHealthPct, a_healthPct);
		// Near death counts when this battle put them there: someone who walked in already at death's door and was
		// not hurt further was not "brought to the edge" by anything that happened here.
		const bool here = p->startHealthPct >= nearDeathPct || p->damageTaken >= 0.05f * p->info.maxHealth;
		if (a_healthPct < nearDeathPct && !p->dead && here) {
			p->nearDeath = true;
			MarkCritical(*p, a_now);
		}
	}

	void Battle::MarkFought(Participant& a_x, Participant& a_y)
	{
		if (a_x.info.side == Side::kPlayer || a_y.info.side == Side::kPlayer) {
			a_x.engaged = a_y.engaged = true;
		} else if (a_x.engaged || a_y.engaged) {
			a_x.involved = a_y.involved = true;
		}
	}

	void Battle::Damage(ActorId a_target, ActorId a_attacker, float a_amount, float a_healthPctAfter, double a_now)
	{
		auto* t = Find(a_target);
		if (!t || a_amount <= 0.0f) return;
		// A blow that empties the victim's health counts for the health it took, not for its full force: the game reports
		// 1,700 points for a strike that finished a bandit with 35 (seen in play), and a few such blows would make one
		// fighter look like the whole battle.
		if (a_healthPctAfter >= 0.0f && a_healthPctAfter <= 0.0f) {
			a_amount = std::min(a_amount, std::max(t->healthPct * t->info.maxHealth, 1.0f));
		}
		t->damageTaken += a_amount;
		t->damageFrom[a_attacker == a_target ? 0 : a_attacker] += a_amount;
		if (a_attacker != 0 && a_attacker != a_target) {
			t->lastAttacker = a_attacker;
			t->lastAttackedAt = a_now;
			if (auto* a = Find(a_attacker)) {
				a->damageDealt += a_amount;
				MarkFought(*t, *a);
			}
		}
		Sample(a_target, a_healthPctAfter, a_now);
	}

	void Battle::Heal(ActorId a_target, ActorId a_healer, float a_amount, const std::string& a_source, double)
	{
		auto* t = Find(a_target);
		if (!t || a_amount <= 0.0f) return;
		t->healingReceived += a_amount;
		auto& h = t->healFrom[a_healer];
		h.amount += a_amount;
		if (t->WasCritical()) h.whileCritical += a_amount;
		if (!a_source.empty()) h.source = a_source;
		if (auto* healer = Find(a_healer)) healer->healingDone += a_amount;
	}

	void Battle::Effect(ActorId a_target, ActorId a_by, const std::string& a_label, const std::string& a_source, bool a_hostile)
	{
		auto* t = Find(a_target);
		if (!t || a_label.empty()) return;
		for (auto& e : t->effects) {
			if (e.label == a_label && e.by == a_by && e.source == a_source) {
				++e.count;
				return;
			}
		}
		t->effects.push_back({ a_label, a_source, a_by, a_hostile, 1 });
		if (a_hostile && a_by != a_target) {
			if (auto* by = Find(a_by)) MarkFought(*Find(a_target), *by);  // Find again: push_back may have moved t
		}
	}

	void Battle::Injury(ActorId a_id, const std::string& a_name, bool a_carried, double a_now)
	{
		auto* p = Find(a_id);
		if (!p || a_name.empty()) return;
		for (const auto& i : p->injuries) {
			if (i.name == a_name) return;
		}
		ActorId by = 0;
		if (!a_carried && p->lastAttacker != 0 && a_now - p->lastAttackedAt <= kAttackerMemory) by = p->lastAttacker;
		p->injuries.push_back({ a_name, by, a_carried });
	}

	void Battle::Dismember(ActorId a_id, bool a_beheaded)
	{
		auto* p = Find(a_id);
		if (!p || !p->dead) return;
		p->dismembered = true;
		if (a_beheaded) p->beheaded = true;
	}

	void Battle::Armed(ActorId a_id)
	{
		if (auto* p = Find(a_id)) p->armed = true;
	}

	bool Battle::Innocent(const Participant& a_p) const
	{
		if (a_p.info.side == Side::kPlayer || !a_p.info.lawful || a_p.armed || a_p.damageDealt > 0.0f || !a_p.kills.empty()) return false;
		if (a_p.damageTaken <= 0.0f && !a_p.dead) return false;  // nobody touched them
		for (const auto& q : participants) {
			for (const auto& e : q.effects) {
				if (e.hostile && e.by == a_p.info.id && q.info.id != a_p.info.id) return false;
			}
		}
		return true;
	}

	void Battle::Down(ActorId a_id, double a_now)
	{
		auto* p = Find(a_id);
		if (!p || p->dead || p->isDown) return;
		p->isDown = true;
		++p->downs;
		MarkCritical(*p, a_now);
	}

	void Battle::Up(ActorId a_id)
	{
		if (auto* p = Find(a_id)) p->isDown = false;
	}

	void Battle::Death(ActorId a_victim, ActorId a_killer, double a_now)
	{
		auto* v = Find(a_victim);
		if (!v || v->dead) return;  // the game reports each death twice (dying, then dead)
		// The game's own death report names the player as the killer for many kills their followers make (seen in play:
		// a player who only healed was credited with every kill). Whoever dealt the blow the victim died of is the
		// killer; the game's word is used only when no blow was seen just before the death (a kill move, a script).
		if (v->lastAttacker != 0 && a_now - v->lastAttackedAt <= kFinalBlow) a_killer = v->lastAttacker;
		else if (a_killer == 0 || a_killer == a_victim) a_killer = RecentAttacker(*v, a_now);
		v->dead = true;
		v->isDown = false;
		v->killer = a_killer;
		v->diedAt = a_now;
		v->minHealthPct = 0.0f;
		if (auto* k = Find(a_killer)) {
			k->kills.push_back(a_victim);
			MarkFought(*v, *k);
		}
	}

	Side Battle::EffectiveSide(const Participant& a_p) const
	{
		if (Innocent(a_p)) return Side::kOther;
		if (a_p.info.side != Side::kOther) return a_p.info.side;
		float toPlayerSide = 0.0f, toEnemies = 0.0f;
		for (const auto& q : participants) {
			const auto it = q.damageFrom.find(a_p.info.id);
			if (it == q.damageFrom.end()) continue;
			if (q.info.side == Side::kPlayer) toPlayerSide += it->second;
			else if (q.info.side == Side::kEnemy) toEnemies += it->second;
		}
		if (toPlayerSide > toEnemies) return Side::kEnemy;
		if (toEnemies > 0.0f) return Side::kPlayer;
		// did no damage to either: judge by who went for them
		for (const auto& [attacker, dmg] : a_p.damageFrom) {
			const auto* q = Find(attacker);
			if (q && q->info.side == Side::kPlayer) return Side::kEnemy;
			if (q && q->info.side == Side::kEnemy) return Side::kPlayer;
		}
		return Side::kOther;
	}

	bool Battle::Involved(const Participant& a_p) const
	{
		return a_p.info.side == Side::kPlayer || a_p.engaged || a_p.involved;
	}

	float Battle::SideDamageDealt(Side a_side) const
	{
		float sum = 0.0f;
		for (const auto& p : participants) {
			if (EffectiveSide(p) == a_side) sum += p.damageDealt;
		}
		return sum;
	}

	int Battle::Count(Side a_side) const
	{
		return static_cast<int>(std::ranges::count_if(participants, [&](const Participant& p) { return Involved(p) && EffectiveSide(p) == a_side; }));
	}

	int Battle::Dead(Side a_side) const
	{
		return static_cast<int>(std::ranges::count_if(participants, [&](const Participant& p) { return p.dead && Involved(p) && EffectiveSide(p) == a_side; }));
	}
}
