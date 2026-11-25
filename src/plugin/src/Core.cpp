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
		// a damage effect's health loss may be reported this much before it took hold or after it ran out (an instant
		// effect's damage lands a frame or two after the effect does; the reports are stamped as they are queued)
		constexpr double kEffectSlack = 0.5;

		// a_target lost health to a_e's caster while a_e was in force
		bool TookHealth(const Participant& a_target, const EffectNote& a_e)
		{
			const ActorId from = a_e.by == a_target.info.id ? 0 : a_e.by;  // self-harm is kept as nobody's (Damage)
			for (const auto& s : a_e.inForce) {
				for (const auto& b : a_target.blows) {
					if (b.by == from && b.at >= s.from && b.at <= s.to) return true;
				}
			}
			return false;
		}

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
			if (!a_info.name.empty() && (!a_info.unnamed || p->info.name.empty())) p->info.name = a_info.name;
			if (a_info.maxHealth > 0.0f) p->info.maxHealth = a_info.maxHealth;
			if (a_info.master != 0) p->info.master = a_info.master;
			if (a_info.lawful) p->info.lawful = true;
			if (a_info.wouldFight) p->info.wouldFight = true;
			// a follower stays a follower through a stray friendly hit; anyone else who turns hostile is an enemy from then on
			if (p->info.side == Side::kOther && a_info.side == Side::kEnemy) p->turnedHostile = true;
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
		// from the brink to half their health or more between two looks (a quarter second apart in game), with no
		// healing just before: something put them back on their feet (a heal reported a moment late takes it back)
		const bool low = p->isDown || p->healthPct < std::max(nearDeathPct, 0.2f);
		if (p->WasCritical() && !p->dead && low && a_healthPct >= 0.5f && (p->lastHealAt < 0.0 || a_now - p->lastHealAt > 1.5)) p->roseAt = a_now;
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
		{
			// a spell held on someone reports every frame (about 800 a minute seen for Flames): one blow per attacker per
			// quarter second is enough for TookHealth, and the oldest go past a limit
			const ActorId by = a_attacker == a_target ? 0 : a_attacker;
			if (t->blows.empty() || t->blows.back().by != by || a_now - t->blows.back().at >= 0.25) t->blows.push_back({ by, a_now });
			if (t->blows.size() > 1024) t->blows.erase(t->blows.begin(), t->blows.begin() + 256);
		}
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

	void Battle::Heal(ActorId a_target, ActorId a_healer, float a_amount, const std::string& a_source, double a_now)
	{
		auto* t = Find(a_target);
		if (!t || a_amount <= 0.0f) return;
		t->lastHealAt = a_now;
		if (t->roseAt >= 0.0 && a_now - t->roseAt <= 1.5) t->roseAt = -1.0;  // healed after all: no rising of their own
		t->healingReceived += a_amount;
		auto& h = t->healFrom[a_healer];
		h.amount += a_amount;
		if (t->WasCritical()) h.whileCritical += a_amount;
		if (!a_source.empty()) h.source = a_source;
		if (auto* healer = Find(a_healer)) healer->healingDone += a_amount;
	}

	std::string Battle::RecentSpell(ActorId a_target, ActorId a_by, double a_now, double a_within) const
	{
		const auto* t = Find(a_target);
		if (!t || a_by == 0) return {};
		const EffectNote* best = nullptr;
		for (const auto& e : t->effects) {
			if (!e.hostile || e.by != a_by || e.source.empty() || e.lastAt < 0.0 || a_now - e.lastAt > a_within) continue;
			if (!best || e.lastAt > best->lastAt) best = &e;
		}
		return best ? best->source : std::string();
	}

	void Battle::Effect(ActorId a_target, ActorId a_by, const std::string& a_label, const std::string& a_source, bool a_hostile, double a_now,
		const EffectFacts& a_facts)
	{
		auto* t = Find(a_target);
		if (!t || a_label.empty()) return;
		EffectNote* note = nullptr;
		for (auto& e : t->effects) {
			if (e.label == a_label && e.by == a_by && e.source == a_source && e.spell == a_facts.spell) {
				note = &e;
				break;
			}
		}
		const bool first = note == nullptr;
		if (first) {
			note = &t->effects.emplace_back();
			note->label = a_label;
			note->source = a_source;
			note->by = a_by;
			note->hostile = a_hostile;
			note->spell = a_facts.spell;
			note->damage = a_facts.damage;
		}
		note->lastAt = a_now;
		++note->count;
		// while a damage effect is in force, what its target loses to its caster is what it took (TookHealth); a pulse
		// every half second makes one span
		if (a_facts.damage && a_facts.inForce) {
			constexpr double kForever = 1e18;
			const EffectNote::Span s = a_now < 0.0 ? EffectNote::Span{ -kForever, kForever } : EffectNote::Span{ a_now - kEffectSlack, a_now + std::max(a_facts.lasts, 0.0) + kEffectSlack };
			if (!note->inForce.empty() && s.from <= note->inForce.back().to) note->inForce.back().to = std::max(note->inForce.back().to, s.to);
			else note->inForce.push_back(s);
		}
		if (!first) return;
		// a damage effect is no sign of a fight until it takes health, and the blow that does marks them (Damage)
		if (a_hostile && a_by != a_target && !(ignoreHarmless && a_facts.damage)) {
			if (auto* by = Find(a_by)) MarkFought(*Find(a_target), *by);  // Find again: push_back may have moved t
		}
	}

	bool Battle::Landed(const Participant& a_target, const EffectNote& a_e) const
	{
		if (!ignoreHarmless || !a_e.hostile) return true;
		if (a_e.damage) return TookHealth(a_target, a_e);
		if (Aura(a_e)) return false;
		if (a_e.spell == 0) return true;
		// the spell's other effects go with its damage: told when one of its damage effects took health
		bool carriesDamage = false;
		for (const auto& d : a_target.effects) {
			if (!d.damage || d.by != a_e.by || d.spell != a_e.spell) continue;
			if (TookHealth(a_target, d)) return true;
			carriesDamage = true;
		}
		return !carriesDamage;
	}

	bool Battle::Aura(const EffectNote& a_e) const
	{
		const auto* caster = Find(a_e.by);
		if (!caster || a_e.damage) return false;
		int targets = 0, own = 0, others = 0;
		for (const auto& p : participants) {
			if (p.info.id == caster->info.id) continue;
			if (!std::ranges::any_of(p.effects, [&](const EffectNote& e) { return e.hostile && e.by == a_e.by && e.label == a_e.label; })) continue;
			++targets;
			++(p.info.side == caster->info.side ? own : others);
		}
		return targets >= 6 && own >= 2 && others >= 2;
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

	void Battle::UsedItem(ActorId a_id, const std::string& a_name, bool a_poison, double a_now)
	{
		auto* p = Find(a_id);
		if (!p || a_name.empty()) return;
		auto& list = a_poison ? p->poisons : p->potions;
		auto  it = std::ranges::find_if(list, [&](const ItemUse& u) { return u.name == a_name; });
		if (it == list.end()) it = list.insert(list.end(), { a_name, 0, -1.0e9 });
		if (a_now - it->lastAt > 1.0) ++it->count;  // its other effects, a moment later, are the same drink
		it->lastAt = a_now;
	}

	void Battle::Armed(ActorId a_id)
	{
		if (auto* p = Find(a_id)) p->armed = true;
	}

	void Battle::CameToFight(ActorId a_id)
	{
		if (auto* p = Find(a_id)) p->cameToFight = true;
	}

	bool Battle::Fought(const Participant& a_p) const { return a_p.armed || a_p.cameToFight || Acted(a_p); }

	bool Battle::Touched(const Participant& a_p) const
	{
		return a_p.damageTaken > 0.0f || a_p.dead ||
		       std::ranges::any_of(a_p.effects, [&](const EffectNote& e) { return e.hostile && e.by != a_p.info.id && Landed(a_p, e); });
	}

	bool Battle::Acted(const Participant& a_p) const
	{
		if (a_p.damageDealt > 0.0f || !a_p.kills.empty()) return true;
		for (const auto& q : participants) {  // a summoner fights through what they summoned
			if (q.info.summon && q.info.master == a_p.info.id && (q.damageDealt > 0.0f || !q.kills.empty())) return true;
		}
		for (const auto& q : participants) {
			for (const auto& e : q.effects) {
				if (e.hostile && e.by == a_p.info.id && q.info.id != a_p.info.id && Landed(q, e)) return true;
			}
		}
		return false;
	}

	bool Battle::NonCombatant(const Participant& a_p) const
	{
		if (a_p.info.side == Side::kPlayer || a_p.info.wouldFight) return false;
		// nobody touched them: no blow, no death, and no harmful effect that took hold. A stagger or a weakening (a giant's
		// stomp, a shout) involves its target without a blow: seen in Whiterun, chickens, pigeons and townsfolk listed as
		// "caught up in it" under the setting that leaves bystanders out. One marked as in it only by an aura (Aura) is
		// touched by nothing that counts.
		if (!Touched(a_p) && !a_p.engaged && !a_p.involved) return false;
		return !Fought(a_p);
	}

	bool Battle::Innocent(const Participant& a_p) const
	{
		if (a_p.info.side == Side::kPlayer || !a_p.info.lawful || a_p.info.wouldFight || a_p.armed || a_p.cameToFight || a_p.damageDealt > 0.0f || !a_p.kills.empty()) return false;
		if (a_p.damageTaken <= 0.0f && !a_p.dead) return false;  // nobody touched them
		for (const auto& q : participants) {
			for (const auto& e : q.effects) {
				if (e.hostile && e.by == a_p.info.id && q.info.id != a_p.info.id && Landed(q, e)) return false;
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
		// A summoned creature that ends with health left and no killer named was dismissed, not killed: its summoner fell
		// or its time ran out (seen in play: an atronach struck once, to 68%, vanished the moment its conjurer died, and
		// was credited to the one who had struck it). Nobody gets that one.
		if (v->info.summon && a_killer == 0 && v->healthPct > 0.01f) a_killer = 0;
		else if (v->lastAttacker != 0 && a_now - v->lastAttackedAt <= kFinalBlow) a_killer = v->lastAttacker;
		else if (a_killer == 0 || a_killer == a_victim) a_killer = RecentAttacker(*v, a_now);
		else if (!v->damageFrom.contains(a_killer)) {
			// ...and not even then when the one it names never struck the victim while others did: that is how a follower
			// felled by a giant came to be told as slain by the player
			if (const auto struck = RecentAttacker(*v, a_now); struck != 0) a_killer = struck;
		}
		v->dead = true;
		v->isDown = false;
		v->killer = a_killer;
		if (const auto* k = Find(a_killer); k && k->info.side == Side::kPlayer && !k->info.summon && !SawThem(a_victim, a_killer)) v->killedUnseen = true;
		v->diedAt = a_now;
		v->minHealthPct = 0.0f;
		if (auto* k = Find(a_killer)) {
			k->kills.push_back(a_victim);
			MarkFought(*v, *k);
		}
	}

	Side Battle::EffectiveSide(const Participant& a_p) const { return EffectiveSide(a_p, 0); }

	Side Battle::EffectiveSide(const Participant& a_p, int a_depth) const
	{
		if (Innocent(a_p)) return Side::kOther;
		// Turned hostile to the player during the battle but fought only the player's enemies: an ally. Seen in Whiterun: the
		// town turned on the party after a follower struck a townsman, and Fjori Farroson, who only ever fought the trolls,
		// was told as an enemy who killed one of their own side by accident.
		if (a_p.info.side == Side::kEnemy && a_p.turnedHostile && !a_p.info.wouldFight) {
			float toParty = 0.0f, toFoes = 0.0f;
			bool  harmedParty = false;
			for (const auto& q : participants) {
				if (q.info.id == a_p.info.id) continue;
				if (const auto it = q.damageFrom.find(a_p.info.id); it != q.damageFrom.end()) {
					if (q.info.side == Side::kPlayer) toParty += it->second;
					else if (q.info.side == Side::kEnemy && (!q.turnedHostile || q.info.wouldFight)) toFoes += it->second;
				}
				if (q.info.side == Side::kPlayer) {
					for (const auto& e : q.effects) {
						if (e.hostile && e.by == a_p.info.id && Landed(q, e)) harmedParty = true;
					}
				}
			}
			if (toFoes > 0.0f && toParty <= 0.0f && !harmedParty) return Side::kAlly;
		}
		if (a_p.info.side != Side::kOther) return a_p.info.side;
		// A summon stands where its master stands: Neloth's Ash Guardian fought for Neloth. Judged on its own, an aggressive
		// atronach of someone who is not the player's read as an enemy, and its kills as an enemy's own side's.
		if (a_p.info.summon && a_p.info.master != 0 && a_p.info.master != a_p.info.id && a_depth < 3) {
			if (const auto* m = Find(a_p.info.master)) return EffectiveSide(*m, a_depth + 1);
		}
		if (a_p.info.wouldFight) return Side::kEnemy;  // would have fought the player: killed unaware, still an enemy
		float toPlayerSide = 0.0f, toEnemies = 0.0f;
		for (const auto& q : participants) {
			const auto it = q.damageFrom.find(a_p.info.id);
			if (it == q.damageFrom.end()) continue;
			if (q.info.side == Side::kPlayer) toPlayerSide += it->second;
			else if (q.info.side == Side::kEnemy) toEnemies += it->second;
		}
		// Struck the player's enemies and never turned hostile to the player (Join would have made them an enemy): an ally,
		// however much of their fire caught the party. Seen at Nchardak: Neloth's spells hit the followers more than the
		// spiders, and he was told as an enemy who fled.
		if (toEnemies > 0.0f) return Side::kAlly;
		// Struck only the party: with real enemies about, that was crossfire (a fireball's edge, a stray arrow); with none,
		// they were the one the party fought (a brawl).
		if (toPlayerSide > 0.0f) return HasEnemies() ? Side::kOther : Side::kEnemy;
		// Did no damage to either: one the player's enemies went for is with them. One only the player's side went for is
		// the one they fought, unless the party was fighting real enemies here: then it was crossfire, and they stay a bystander.
		bool byPlayerSide = false;
		for (const auto& [attacker, dmg] : a_p.damageFrom) {
			const auto* q = Find(attacker);
			if (q && q->info.side == Side::kEnemy) return Side::kAlly;
			if (q && q->info.side == Side::kPlayer) byPlayerSide = true;
		}
		return byPlayerSide && !HasEnemies() ? Side::kEnemy : Side::kOther;
	}

	bool Battle::Involved(const Participant& a_p) const
	{
		if (omitBystanders && NonCombatant(a_p)) return false;  // the player's doing to bystanders is no part of the story
		// Neither side, and nothing happened to them or by them: no part of it, whatever combat state the game gave them.
		// Seen in Whiterun: townsfolk only reached by Durak's aura, in combat to watch or flee, listed as "caught up in it".
		if (a_p.info.side == Side::kOther && !Touched(a_p) && !Acted(a_p)) return false;
		return a_p.info.side == Side::kPlayer || a_p.engaged || a_p.involved;
	}

	bool Battle::HasEnemies() const
	{
		return std::ranges::any_of(participants, [&](const Participant& p) { return p.info.side == Side::kEnemy && Involved(p); });
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
