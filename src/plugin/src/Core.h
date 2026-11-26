#pragma once

// The battle model: who fought, what happened to each of them. Plain data and plain rules, no game types, so the
// whole of it (and Narrative.cpp on top) builds and runs in tests/ without Skyrim. Tracker.cpp feeds it.

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace BSM
{
	using ActorId = std::uint32_t;  // a reference FormID; 0 = nobody / unknown

	// Which side an actor is on, as the game sees it when they join.
	enum class Side : std::uint8_t
	{
		kPlayer,  // the player, followers, and anything they command
		kEnemy,   // hostile to the player
		kOther,   // neither: guards, bystanders, wildlife fighting each other
		// Only as told (EffectiveSide), never as joined: someone who is not the player's but fought the player's enemies on
		// their own account (Neloth at Nchardak, Whiterun's guards at a dragon). Listed apart from the party, with their own
		// kills, and kept out of the party's shares and roles.
		kAlly
	};

	struct ActorInfo
	{
		ActorId     id{ 0 };
		std::string name;
		Side        side{ Side::kOther };
		bool        isPlayer{ false };
		ActorId     master{ 0 };  // the summoner / reanimator, when this is a commanded actor
		float       maxHealth{ 1.0f };
		float       healthPct{ 1.0f };  // their health share when they joined
		bool        lawful{ false };    // belongs to a hold's law (a citizen, a guard): killing them is murder, unlike a bandit or a beast
		// Would fight the player on sight, noticed or not: their factions count the player an enemy, or they attack anyone.
		// A bandit killed in his sleep was still an enemy.
		bool        wouldFight{ false };
		// A summoned or raised creature (a conjured atronach, a companion's mechanical spider): not a fighter of its own on
		// the player's side, and its end is no death. Its master is in `master` when the game names one. Decided when it first
		// joins: a corpse a necromancer raises later keeps its first death as the kill it was.
		bool        summon{ false };
		// The game gave no name; `name` holds the stand-in ("someone"), which never replaces a name already known: a body
		// cleaned up mid-battle reports nameless, and Isidor was told as "someone" (Whiterun, 1.6.0-beta-1).
		bool        unnamed{ false };
	};

	// One kind of magic applied to a participant by one actor: "burned by fire" by 00012345 with "Flames", 4 times.
	struct EffectNote
	{
		std::string label;
		std::string source;  // the spell, potion, poison or enchantment's name; may be empty
		ActorId     by{ 0 };
		bool        hostile{ false };
		int         count{ 0 };
		double      lastAt{ -1.0 };  // when it last took hold; -1 = not known
		std::uint32_t spell{ 0 };    // what carried it (spell, potion, enchantment), by form; 0 = not known
		// A damage effect: it lowers health (drained of life, burned, frozen, shocked, poisoned, hurt). Seen in play:
		// something on the player pulsed an Absorb Health effect onto everyone nearby every half second, damaging none
		// of them, and each was told the player had drained their life. So one is told only when it took health.
		bool damage{ false };
		struct Span
		{
			double from, to;
		};
		std::vector<Span> inForce;  // a damage effect's times on the target while it did something, with a little slack
	};

	// What the tracker could read about one application of an effect, beyond its name.
	struct EffectFacts
	{
		std::uint32_t spell{ 0 };
		bool          damage{ false };
		bool          inForce{ true };  // the effect on the target has a magnitude and its conditions hold (or it is gone: not known)
		double        lasts{ 0.0 };     // its duration in seconds; 0 = instant
	};

	// A potion drunk or a poison used, by name, and how many times.
	struct ItemUse
	{
		std::string name;
		int         count{ 0 };
		double      lastAt{ -1.0e9 };
	};

	// A lasting injury an injury mod put on a participant ("Leg Injury", "Major Injury").
	struct InjuryNote
	{
		std::string name;
		ActorId     by{ 0 };          // whose blows did it, when one landed just before; 0 = not known
		bool        carried{ false };  // they brought it into the battle
	};

	struct Participant
	{
		ActorInfo info;

		float damageTaken{ 0.0f };
		float damageDealt{ 0.0f };
		float healingReceived{ 0.0f };
		float healingDone{ 0.0f };

		std::map<ActorId, float> damageFrom;  // attacker -> damage; 0 = no attacker (falls, traps, lingering effects)
		struct Blow
		{
			ActorId by;  // as in damageFrom
			double  at;
		};
		std::vector<Blow> blows;  // every loss of health, when it came: what a damage effect took is read from these

		struct Heal
		{
			float       amount{ 0.0f };
			float       whileCritical{ 0.0f };  // the part received after this participant was near death or down
			std::string source;
		};
		std::map<ActorId, Heal> healFrom;

		float  startHealthPct{ 1.0f };  // what they brought into the battle
		float  healthPct{ 1.0f };       // as last seen
		float  minHealthPct{ 1.0f };
		bool   nearDeath{ false };  // health fell below the near-death share while still standing
		int    downs{ 0 };          // times knocked into bleedout
		bool   isDown{ false };
		double criticalAt{ -1.0 };  // when they first were near death or down; -1 = never
		ActorId broughtLowBy{ 0 };  // who dealt the blows that did it
		// Back from bleedout or death's door at a stroke, with half their health or more and nobody's healing behind it:
		// a death-alternative mod (Stay In The Fight, Sacrosanct's Fortitude, Last Man Standing) putting them back on
		// their feet. When it happened; -1 = never.
		double roseAt{ -1.0 };
		double lastHealAt{ -1.0 };

		// Joined as neither side and turned hostile to the player later (a townsman the town turned on the party with). An
		// enemy from then on, unless all they fought was the player's enemies (EffectiveSide).
		bool turnedHostile{ false };

		bool    dead{ false };
		ActorId killer{ 0 };
		std::string killedWith;  // the spell or weapon that did it ("Fireball", "Steel Greatsword"); empty = not known
		double  diedAt{ -1.0 };

		// what a dismemberment mod (or the game's own beheading) did to the body
		bool dismembered{ false };
		bool beheaded{ false };

		std::vector<ActorId>    kills;
		std::vector<EffectNote> effects;
		std::vector<InjuryNote> injuries;
		std::vector<ItemUse>    potions;  // drunk in this battle
		std::vector<ItemUse>    poisons;  // put on a weapon and landed on a foe

		// Fought the player's side directly (traded a blow, a kill or a hostile spell with one of them)...
		// seen with a weapon, spell or fists raised during the battle
		bool armed{ false };

		// entered combat against someone of their own accord, before anyone had struck them, and was not running away
		bool cameToFight{ false };

		bool engaged{ false };
		// ...or fought someone who did. Anything further removed (prey of a predator that never touched the party) is not
		// part of this battle's story, whatever combat state the game put it in.
		bool involved{ false };

		ActorId lastAttacker{ 0 };
		double  lastAttackedAt{ -1.0 };

		[[nodiscard]] bool WasCritical() const { return criticalAt >= 0.0; }
	};

	class Battle
	{
	public:
		int         id{ 0 };
		double      startedAt{ 0.0 };  // seconds, any monotonic clock
		double      endedAt{ -1.0 };   // -1 while it is still being fought
		float       endedGameHours{ 0.0f };
		std::string location;
		float       nearDeathPct{ 0.15f };
		// A damage effect that took no health is left out: not told, and no sign that anyone fought (Landed).
		bool        ignoreHarmless{ true };  // always on in game; the offline tests also run without it
		// Non-combatants (NonCombatant) are left out: not listed, not counted, not told. Off: the one killed is told as not
		// actively engaged in combat (Innocent).
		bool        omitBystanders{ false };
		// The player was moved away from it (a teleport, a door, a death-alternative mod) while it was being fought: it
		// ended there, and enemies still alive are not told as having fled.
		bool        cutShort{ false };
		// Fighting that stopped and started again within the wind-down (a dungeon cleared room by room, enemies arriving in
		// groups): one battle, fought in this many clashes.
		int         clashes{ 1 };
		// Who has seen whom of the player's side (Saw). Someone outside the party who never saw them is told "an unseen
		// attacker" in their place: abac's report, enemies searching for a sneak archer learned the player's name from the
		// summary. Off (the offline tests unless they turn it on): everyone saw everyone.
		bool        trackSight{ false };
		// one of the player's side -> who has detected them, and when first. By the time: a witness who saw the player only
		// after a kill is not told who made it (the Sleeping Giant: Sven killed unseen, the inn told it was the
		// player because they saw them in the fight that followed).
		std::map<ActorId, std::map<ActorId, double>> seenBy;

		std::vector<Participant> participants;  // in the order they joined

		[[nodiscard]] bool Ongoing() const { return endedAt < 0.0; }

		// Records that a_viewer detected a_target at a_at (left out: from before the battle). True when it is the first time.
		bool Saw(ActorId a_viewer, ActorId a_target, double a_at = -1.0e9)
		{
			auto&      seen = seenBy[a_target];
			const auto [it, first] = seen.emplace(a_viewer, a_at);
			if (!first && a_at < it->second) it->second = a_at;
			return first;
		}
		// at any time in the battle
		[[nodiscard]] bool SawThem(ActorId a_viewer, ActorId a_target) const { return SawBy(a_viewer, a_target, 1.0e18); }
		// by a_at (a kill's moment): sight is sampled, so a moment's slack
		[[nodiscard]] bool SawBy(ActorId a_viewer, ActorId a_target, double a_at) const
		{
			if (!trackSight || a_viewer == a_target) return true;
			const auto it = seenBy.find(a_target);
			if (it == seenBy.end()) return false;
			const auto at = it->second.find(a_viewer);
			return at != it->second.end() && at->second <= a_at + 0.3;
		}
		// killed by one of the player's side they had never seen (a sneak attack, an arrow from the dark)
		[[nodiscard]] bool KilledUnseen(const Participant& a_victim) const;

		[[nodiscard]] Participant*       Find(ActorId a_id);
		[[nodiscard]] const Participant* Find(ActorId a_id) const;

		// Adds the actor, or refreshes name / side / max health of one already in. A side never goes back from enemy.
		Participant& Join(const ActorInfo& a_info);

		// a_healthPctAfter < 0 = not known
		void Damage(ActorId a_target, ActorId a_attacker, float a_amount, float a_healthPctAfter, double a_now);
		void Sample(ActorId a_id, float a_healthPct, double a_now);
		void Heal(ActorId a_target, ActorId a_healer, float a_amount, const std::string& a_source, double a_now);
		void Effect(ActorId a_target, ActorId a_by, const std::string& a_label, const std::string& a_source, bool a_hostile, double a_now = -1.0,
			const EffectFacts& a_facts = {});
		// The effect is worth telling. With ignoreHarmless, a damage effect only when it took health from a_target: it was
		// in force (a magnitude, its conditions met) and a_target lost health to its caster while it was. The other effects
		// of its spell (a frost spell's slow) go with it. Judged when told: the damage can come after the effect.
		[[nodiscard]] bool Landed(const Participant& a_target, const EffectNote& a_e) const;
		// A harmless effect one caster put on many around them, their own kind and others alike: an aura or a pulse, not
		// something done to anyone. Seen in Whiterun: Durak's "armor weakened" took hold 448 times in a minute, on the party,
		// the townsfolk, the chickens and the cows. Not told, and no sign that anyone fought (Landed).
		[[nodiscard]] bool Aura(const EffectNote& a_e) const;
		// the spell (or poison, or enchantment) whose harmful magic a_by last put on a_target within a_within seconds
		// before a_now; empty when there was none
		[[nodiscard]] std::string RecentSpell(ActorId a_target, ActorId a_by, double a_now, double a_within) const;
		// a_carried: they already had it when they joined
		void Injury(ActorId a_id, const std::string& a_name, bool a_carried, double a_now);
		// the dead participant's body lost a limb (a_beheaded: the head)
		void Dismember(ActorId a_id, bool a_beheaded);
		void Armed(ActorId a_id);
		// a potion (or poison) taking hold; the several effects of one potion, arriving together, are one use
		void UsedItem(ActorId a_id, const std::string& a_name, bool a_poison, double a_now);
		[[nodiscard]] bool Fought(const Participant& a_p) const;  // did anything hostile in this battle
		// Something in this battle reached them: a blow, a death, or a harmful effect that took hold (an aura is none).
		[[nodiscard]] bool Touched(const Participant& a_p) const;
		// They did something in it themselves: a blow, a kill, or a harmful effect that took hold on someone (their
		// summons' doing counts). Being in combat (armed, came to fight) is not a deed.
		[[nodiscard]] bool Acted(const Participant& a_p) const;
		// Harmed or killed in it without being a fighter: not on the player's side, not someone who would fight the player,
		// and did nothing hostile. Left out of the battle altogether with omitBystanders.
		[[nodiscard]] bool NonCombatant(const Participant& a_p) const;
		void CameToFight(ActorId a_id);
		void Down(ActorId a_id, double a_now);
		void Up(ActorId a_id);
		void Death(ActorId a_victim, ActorId a_killer, double a_now);

		// The side used for telling the story. An "other" who struck the player's enemies (or was only attacked by them) is
		// an ally, whatever their crossfire did to the party; a summon stands where its master stands. One who struck only
		// the player's side is an enemy, unless the party was fighting real enemies (HasEnemies): then it was crossfire, and
		// they stay an "other". So is one who fought nobody and was struck only by the player's side.
		[[nodiscard]] Side EffectiveSide(const Participant& a_p) const;

		// Took part in this battle. The player's side always counts; anyone else must have fought the player's side, or
		// fought someone who did (a deer that only bolted is not an enemy, and neither is a crab a wolf killed nearby).
		[[nodiscard]] bool Involved(const Participant& a_p) const;

		// Someone in it who turned hostile to the player (as the game saw them, not as the story tells them) took part:
		// the party had real enemies to fight here, not only whoever it went for.
		[[nodiscard]] bool HasEnemies() const;

		// A lawful person who was attacked without fighting: never raised a weapon, never came at anyone, struck nobody,
		// cast nothing harmful. (Told as "not actively engaged in combat": whether they were innocent is for the story.)
		// Not one of the enemies, whatever the game made of them once they were struck.
		[[nodiscard]] bool Innocent(const Participant& a_p) const;

		[[nodiscard]] float SideDamageDealt(Side a_side) const;
		[[nodiscard]] int   Count(Side a_side) const;  // involved participants only
		[[nodiscard]] int   Dead(Side a_side) const;

	private:
		[[nodiscard]] Side EffectiveSide(const Participant& a_p, int a_depth) const;
		void MarkCritical(Participant& a_p, double a_now);
		void MarkFought(Participant& a_x, Participant& a_y);
	};
}
