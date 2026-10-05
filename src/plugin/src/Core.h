#pragma once

// The battle model: who fought, what happened to each of them. Plain data and plain rules, no game types, so the
// whole of it (and Narrative.cpp on top) builds and runs in tests/ without Skyrim. Tracker.cpp feeds it.

#include <cstdint>
#include <map>
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
		kOther    // neither: guards, bystanders, wildlife fighting each other
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
	};

	// One kind of magic applied to a participant by one actor: "burned by fire" by 00012345 with "Flames", 4 times.
	struct EffectNote
	{
		std::string label;
		std::string source;  // the spell, potion, poison or enchantment's name; may be empty
		ActorId     by{ 0 };
		bool        hostile{ false };
		int         count{ 0 };
	};

	struct Participant
	{
		ActorInfo info;

		float damageTaken{ 0.0f };
		float damageDealt{ 0.0f };
		float healingReceived{ 0.0f };
		float healingDone{ 0.0f };

		std::map<ActorId, float> damageFrom;  // attacker -> damage; 0 = no attacker (falls, traps, lingering effects)

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

		bool    dead{ false };
		ActorId killer{ 0 };
		double  diedAt{ -1.0 };

		std::vector<ActorId>    kills;
		std::vector<EffectNote> effects;

		// Fought the player's side directly (traded a blow, a kill or a hostile spell with one of them)...
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

		std::vector<Participant> participants;  // in the order they joined

		[[nodiscard]] bool Ongoing() const { return endedAt < 0.0; }

		[[nodiscard]] Participant*       Find(ActorId a_id);
		[[nodiscard]] const Participant* Find(ActorId a_id) const;

		// Adds the actor, or refreshes name / side / max health of one already in. A side never goes back from enemy.
		Participant& Join(const ActorInfo& a_info);

		// a_healthPctAfter < 0 = not known
		void Damage(ActorId a_target, ActorId a_attacker, float a_amount, float a_healthPctAfter, double a_now);
		void Sample(ActorId a_id, float a_healthPct, double a_now);
		void Heal(ActorId a_target, ActorId a_healer, float a_amount, const std::string& a_source, double a_now);
		void Effect(ActorId a_target, ActorId a_by, const std::string& a_label, const std::string& a_source, bool a_hostile);
		void Down(ActorId a_id, double a_now);
		void Up(ActorId a_id);
		void Death(ActorId a_victim, ActorId a_killer, double a_now);

		// The side used for telling the story: an "other" who fought the player's side counts as an enemy, one who only
		// fought the player's enemies counts with the player's side.
		[[nodiscard]] Side EffectiveSide(const Participant& a_p) const;

		// Took part in this battle. The player's side always counts; anyone else must have fought the player's side, or
		// fought someone who did (a deer that only bolted is not an enemy, and neither is a crab a wolf killed nearby).
		[[nodiscard]] bool Involved(const Participant& a_p) const;

		[[nodiscard]] float SideDamageDealt(Side a_side) const;
		[[nodiscard]] int   Count(Side a_side) const;  // involved participants only
		[[nodiscard]] int   Dead(Side a_side) const;

	private:
		void MarkCritical(Participant& a_p, double a_now);
		void MarkFought(Participant& a_x, Participant& a_y);
	};
}
