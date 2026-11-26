// Offline tests of the battle model and its narrative: no game, no SkyrimNet. Run by build.ps1 before the DLL is
// accepted. Each scenario replays a fight as the tracker would report it and checks what an NPC is then told.

#define _CRT_SECURE_NO_WARNINGS
#include "../src/Core.h"
#include "../src/Narrative.h"
#include "../src/Text.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

using namespace BSM;

namespace
{
	int g_failed = 0;
	int g_checks = 0;

	void Check(bool a_ok, const char* a_what, const std::string& a_text)
	{
		++g_checks;
		if (a_ok) return;
		++g_failed;
		std::printf("FAIL: %s\n----\n%s\n----\n", a_what, a_text.c_str());
	}

	bool Has(const std::string& a_text, const char* a_needle) { return a_text.find(a_needle) != std::string::npos; }

	constexpr ActorId kHero = 0x14, kLydia = 0xA2C94, kChief = 0xFF000801, kBandit1 = 0xFF000802, kBandit2 = 0xFF000803, kMage = 0xFF000804,
					  kAtronach = 0xFF000805, kGuard = 0xFF000806;

	ActorInfo Info(ActorId a_id, const char* a_name, Side a_side, float a_hp, ActorId a_master = 0)
	{
		return { a_id, a_name, a_side, a_id == kHero, a_master, a_hp };
	}

	// what the tracker reads off a damage effect: whether it was doing anything (a magnitude, its conditions met), what
	// carried it, how long it lasts
	EffectFacts Damaging(bool a_inForce, std::uint32_t a_spell = 0, double a_lasts = 0.0) { return { a_spell, true, a_inForce, a_lasts }; }

	// The fight the mod exists for: Lydia is beaten into bleedout by the chief, Kaira kills the chief and heals her.
	Battle Rescue()
	{
		Battle b;
		b.id = 1;
		b.startedAt = 100.0;
		b.location = "Bleak Falls Barrow";
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		b.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
		b.Join(Info(kChief, "Bandit Chief", Side::kEnemy, 400));
		b.Join(Info(kBandit1, "Bandit", Side::kEnemy, 100));
		b.Join(Info(kBandit2, "Bandit", Side::kEnemy, 100));
		b.Join(Info(kMage, "Bandit Mage", Side::kEnemy, 80));

		b.Damage(kBandit1, kLydia, 100, 0.0f, 103);
		b.Death(kBandit1, kLydia, 103);
		b.Death(kBandit1, kLydia, 103.1);  // the game's second report of the same death
		b.Effect(kLydia, kMage, "burned by fire", "Flames", true);
		b.Effect(kLydia, kMage, "burned by fire", "Flames", true);
		b.Damage(kLydia, kMage, 40, 0.86f, 104);
		b.Damage(kLydia, kChief, 150, 0.36f, 106);
		b.Damage(kLydia, kChief, 90, 0.06f, 108);
		b.Down(kLydia, 108.2);
		b.Damage(kHero, kBandit2, 30, 0.85f, 109);
		b.Effect(kHero, kHero, "shielded by magical armor", "Stoneflesh", false);
		b.Damage(kBandit2, kHero, 100, 0.0f, 110);
		b.Death(kBandit2, kHero, 110);
		b.Damage(kMage, kHero, 80, 0.0f, 112);
		b.Death(kMage, kHero, 112);
		b.Damage(kChief, kHero, 400, 0.0f, 118);
		b.Death(kChief, kHero, 118);
		b.Heal(kLydia, kHero, 240, "Healing Hands", 121);
		b.Up(kLydia);
		b.endedAt = 128.0;
		return b;
	}

	void TestRescue()
	{
		const auto b = Rescue();
		const NarrativeOptions opt;

		const auto lydia = SummaryText(BuildSummary(b, kLydia, "Lydia", opt));
		Check(Has(lydia, "Lydia was struck down by Bandit Chief"), "Lydia is told she was struck down, and by whom", lydia);
		Check(Has(lydia, "Bandit Chief, who had struck down Lydia, was then killed by Kaira."), "the chief's death is credited to Kaira", lydia);
		Check(Has(lydia, "Kaira healed Lydia with Healing Hands"), "the healing is credited to Kaira, with the spell", lydia);
		Check(Has(lydia, "Kaira saved Lydia's life in this battle"), "the plain verdict", lydia);
		Check(Has(lydia, "Lydia killed 1 of the 4 who died on the other side: Bandit."), "Lydia's own kill count", lydia);
		Check(Has(lydia, "Lydia suffered: burned by fire by Bandit Mage (Flames)."), "hostile effects, once, with caster and spell", lydia);
		Check(Has(lydia, "Who killed the enemies: Kaira 3 (Bandit Chief, Bandit and Bandit Mage); Lydia 1 (Bandit)."), "kill tally, most kills first", lydia);
		Check(Has(lydia, "All 4 enemies were killed."), "outcome", lydia);
		Check(Has(lydia, "Kaira and Lydia fought against Bandit Chief, Bandit x2 and Bandit Mage."), "sides, same names counted", lydia);
		Check(Has(lydia, "Kaira: took only light wounds; dealt most of their side's damage; was never near death; killed 3 of the other side."), "the player's line among the others", lydia);
		Check(!Has(lydia, "points"), "no raw figures unless asked for", lydia);

		const auto hero = SummaryText(BuildSummary(b, kHero, "Kaira", opt));
		Check(Has(hero, "Kaira was never in serious danger."), "Kaira was not in danger", hero);
		Check(Has(hero, "Kaira saved Lydia's life. Bandit Chief, who had struck down Lydia, was then killed by Kaira."), "Kaira's summary says whose life he saved", hero);
		Check(Has(hero, "Kaira was helped by: shielded by magical armor (own Stoneflesh)."), "a self-cast protection", hero);
		Check(Has(hero, "Lydia: took more damage") || Has(hero, "Lydia: was badly hurt"), "Lydia's line among the others", hero);
		Check(Has(hero, "was saved by Kaira"), "Lydia's line says who saved her", hero);

		const auto witness = BuildSummary(b, 0x1A66B, "Hulda", opt);
		Check(witness.show && !witness.participant && witness.personal.empty() && witness.others.size() == 2, "a witness gets the general lines and both fighters",
			SummaryText(witness));

		const auto summary = BuildMemory(b, opt);
		Check(Has(summary, "but Kaira killed Bandit Chief and Kaira healed Lydia: Kaira saved Lydia's life."), "the summary names the rescue", summary);
		Check(Significant(b, 3, 30.0), "a rescue is worth remembering", summary);

		NarrativeOptions numbers;
		numbers.showNumbers = true;
		const auto withNumbers = SummaryText(BuildSummary(b, kLydia, "Lydia", numbers));
		Check(Has(withNumbers, "(about 280 points)"), "figures when asked for", withNumbers);
	}

	// Near death without bleedout, pulled through alone; an atronach's kill is credited with its master.
	void TestNearDeathAndSummon()
	{
		Battle b;
		b.startedAt = 0;
		b.location = "the Whiterun plains";
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		b.Join(Info(kAtronach, "Flame Atronach", Side::kPlayer, 150, kHero));
		b.Join(Info(kBandit1, "Wolf", Side::kEnemy, 50));
		b.Join(Info(kBandit2, "Wolf", Side::kEnemy, 50));
		b.Damage(kHero, kBandit1, 175, 0.125f, 3);
		b.Damage(kBandit1, kAtronach, 50, 0, 5);
		b.Death(kBandit1, kAtronach, 5);
		b.Heal(kHero, kHero, 120, "Potion of Healing", 6);
		b.Damage(kBandit2, kHero, 20, 0.6f, 8);
		b.endedAt = 30;

		const auto hero = SummaryText(BuildSummary(b, kHero, "Kaira", {}));
		Check(Has(hero, "Kaira was brought to the very edge of death by the first Wolf"), "near death without bleedout", hero);
		Check(Has(hero, "The first Wolf, who had nearly killed Kaira, was then killed by Flame Atronach (commanded by Kaira)."), "the summon's kill, with its master", hero);
		Check(!Has(hero, "saved Kaira's life"), "your own summon's kill is not someone else's rescue", hero);
		Check(Has(hero, "Kaira healed themself with Potion of Healing"), "a potion is a self heal", hero);
		Check(Has(hero, "1 of the 2 enemies were killed; the rest fled, yielded or broke off."), "partial outcome", hero);
		Check(Has(hero, "Flame Atronach (commanded by Kaira) 1 (Wolf)"), "kill tally credits the summon with its master", hero);
	}

	// A guard who only fought the bandits is an ally: told apart from the party. A townsman whose blow caught the player while
	// bandits were about was crossfire, not an enemy.
	void TestOthers()
	{
		Battle b;
		b.startedAt = 0;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		b.Join(Info(kBandit1, "Bandit", Side::kEnemy, 100));
		b.Join(Info(kGuard, "Whiterun Guard", Side::kOther, 200));
		b.Join(Info(kMage, "Angry Farmer", Side::kOther, 80));
		b.Damage(kHero, kBandit1, 5, 0.97f, 1);  // the bandit fights the player; the guard then deals with the bandit
		b.Damage(kBandit1, kGuard, 100, 0, 2);
		b.Death(kBandit1, kGuard, 2);
		b.Damage(kHero, kMage, 10, 0.95f, 3);
		b.endedAt = 10;
		Check(b.EffectiveSide(*b.Find(kGuard)) == Side::kAlly, "the guard fought beside the player, as an ally", "");
		Check(b.EffectiveSide(*b.Find(kMage)) == Side::kOther, "the farmer's blow, with bandits about, was crossfire", "");
		const auto hero = SummaryText(BuildSummary(b, kHero, "Kaira", {}));
		Check(Has(hero, "Kaira fought against Bandit.") && Has(hero, "Whiterun Guard also fought them, apart from the party.")
				&& Has(hero, "Who killed the enemies: Whiterun Guard 1 (Bandit)."),
			"the ally told apart, with their kill", hero);
		Check(Has(hero, "Kaira killed none of the 1 who died on the other side."), "no kills is said outright", hero);

		// a follower who turns up in a later report as an enemy stays a follower; an other who turns hostile becomes an enemy
		b.Join(Info(kHero, "Kaira", Side::kEnemy, 200));
		b.Join(Info(kGuard, "Whiterun Guard", Side::kEnemy, 200));
		Check(b.Find(kHero)->info.side == Side::kPlayer && b.Find(kGuard)->info.side == Side::kEnemy, "side changes on rejoin", "");
	}

	// Seen in play: a camp of uniquely named bandits, a deer that only bolted, a long roll of names.
	void TestBigFightAndBystanders()
	{
		Battle b;
		b.startedAt = 0;
		b.location = "Halted Stream Camp";
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		b.Join(Info(kLydia, "Lydia", Side::kPlayer, 130));
		const char* names[] = { "Enjaadia [bandit]", "Darkhu [Bandit Outlaw]", "Marana [bandit]", "Ahtar [Bandit Outlaw]", "Bakhig [Bandit Outlaw]", "Dunius [Bandit Outlaw]" };
		for (ActorId i = 0; i < 6; ++i) {
			b.Join(Info(0xFF001000 + i, names[i], Side::kEnemy, 100));
			b.Damage(0xFF001000 + i, kHero, 100, 0.0f, 5.0 + i);
			b.Death(0xFF001000 + i, kHero, 5.0 + i);
		}
		b.Join(Info(0x846F6, "Deer", Side::kEnemy, 50));  // "in combat" with the player only because it fled
		b.Damage(kHero, 0xFF001003, 20, 0.9f, 4);
		b.endedAt = 40;

		Check(b.Count(Side::kEnemy) == 6 && !b.Involved(*b.Find(0x846F6)), "a deer that only fled is not an enemy", "");
		const auto me = BuildSummary(b, kHero, "Kaira", {});
		const auto text = SummaryText(me);
		Check(Has(text, "Kaira and Lydia fought against Enjaadia [bandit], Darkhu [Bandit Outlaw], Marana [bandit], Ahtar [Bandit Outlaw] and 2 others."),
			"a long roll of enemies is cut short", text);
		Check(!Has(text, "Deer"), "the deer is not told", text);
		Check(Has(text, "All 6 enemies were killed.") && Has(text, "Who killed the enemies: Kaira 6."), "outcome and tally without the long name list", text);
		Check(Has(text, "Kaira killed 6 of the 6 who died on the other side."), "own kills without the long name list", text);
		Check(me.others.size() == 1 && Has(text, "None of them was seriously hurt or in any real danger."), "an easy fight needs no roll call", text);

		// five names are told in full: "and 1 others" would be no shorter
		Battle c;
		c.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		for (ActorId i = 0; i < 5; ++i) {
			c.Join(Info(0xFF002000 + i, names[i], Side::kEnemy, 100));
			c.Damage(0xFF002000 + i, kHero, 10, 0.9f, 1);
		}
		c.endedAt = 10;
		const auto five = SummaryText(BuildSummary(c, kHero, "Kaira", {}));
		Check(Has(five, "Ahtar [Bandit Outlaw] and Bakhig [Bandit Outlaw]."), "five names are told in full", five);
		Check(!Has(five, "None of them"), "alone on your side: no line about the others", five);
	}

	// Seen in play on a creature-heavy list: predators and prey all around a fight are "in combat", some with the
	// player as their target, and fight each other. Only what fought the party, or fought those who did, is the battle.
	void TestWildlifeAroundAFight()
	{
		constexpr ActorId kMinotaur = 0xFF003001, kElk = 0xFF003002, kCrab = 0xFF003003, kEchatere = 0xFF003004, kHorse = 0xFF003005;
		Battle b;
		b.startedAt = 0;
		b.location = "the wilds of Skyrim";
		auto hero = Info(kHero, "Kaira", Side::kPlayer, 100);
		hero.healthPct = 0.12f;  // still hurt from the last fight
		b.Join(hero);
		b.Join(Info(kLydia, "Jenassa", Side::kPlayer, 200));
		b.Join(Info(kMinotaur, "Minotaur", Side::kEnemy, 300));
		b.Join(Info(kElk, "Elk", Side::kEnemy, 60));          // "in combat" with the player only because it fled
		b.Join(Info(kCrab, "Crab", Side::kEnemy, 20));
		b.Join(Info(kEchatere, "Echatere", Side::kEnemy, 80));
		b.Join(Info(kHorse, "Horse", Side::kOther, 100));
		b.Sample(kHero, 0.12f, 1);
		b.Damage(kMinotaur, kHero, 300, 0.0f, 5);
		b.Death(kMinotaur, kHero, 5);
		b.Damage(kCrab, kEchatere, 20, 0.0f, 6);   // a predator and its prey: nothing to do with the party
		b.Death(kCrab, kEchatere, 6);
		b.Damage(kHorse, kEchatere, 30, 0.7f, 7);
		b.endedAt = 60;

		Check(b.Count(Side::kEnemy) == 1 && b.Dead(Side::kEnemy) == 1, "only what fought the party is an enemy", "");
		Check(!b.Find(kHero)->WasCritical(), "walking in at death's door is not being brought there", "");
		const auto me = BuildSummary(b, kHero, "Kaira", {});
		const auto text = SummaryText(me);
		Check(Has(SummaryText(BuildSummary([] {
			Battle o;
			o.Join(Info(kLydia, "Jenassa", Side::kPlayer, 200));  // the follower joined first
			o.Join(Info(kHero, "Kaira", Side::kPlayer, 100));
			o.Join(Info(kBandit1, "Bandit", Side::kEnemy, 50));
			o.Damage(kBandit1, kLydia, 10, 0.8f, 1);
			o.endedAt = 5;
			return o;
		}(), kHero, "Kaira", {})), "Kaira and Jenassa fought against Bandit."), "the player leads the list, whoever joined first", "");
		Check(Has(text, "Kaira and Jenassa fought against Minotaur.") && Has(text, "The one enemy was killed.") && Has(text, "Who killed the enemies: Kaira 1 (Minotaur)."),
			"the battle is the party against the minotaur", text);
		Check(!Has(text, "Echatere") && !Has(text, "Crab") && !Has(text, "Elk") && !Has(text, "Horse") && !Has(text, "caught up"), "the wildlife's own quarrels are not told", text);
		Check(Has(text, "Kaira went into this fight already close to death from earlier wounds") && !Has(text, "edge of death"), "already wounded going in", text);
		const auto elk = BuildSummary(b, kElk, "Elk", {});
		Check(!elk.participant && elk.personal.empty(), "what never fought is a witness, not a fighter", SummaryText(elk));
		const auto other = SummaryText(BuildSummary(b, kLydia, "Jenassa", {}));
		Check(Has(other, "Kaira: took no damage worth the name; dealt most of their side's damage; went in already close to death from earlier wounds; killed 1 of the other side."), "the companion's line", other);

		// someone who fights an enemy of the party is part of it; hurt further while already low counts as near death
		Battle c;
		c.Join(hero);
		c.Join(Info(kMinotaur, "Minotaur", Side::kEnemy, 300));
		c.Join(Info(kGuard, "Whiterun Guard", Side::kOther, 200));
		c.Damage(kHero, kMinotaur, 8, 0.04f, 1);
		c.Damage(kMinotaur, kGuard, 50, 0.8f, 2);
		c.endedAt = 10;
		Check(c.Involved(*c.Find(kGuard)) && c.EffectiveSide(*c.Find(kGuard)) == Side::kAlly, "a guard who fights the party's enemy is an ally", "");
		Check(c.Find(kHero)->WasCritical() && c.Find(kHero)->broughtLowBy == kMinotaur, "hurt further while already low is near death", "");
	}

	// Reported by a player: "NPCs think I killed everyone myself, even if I am just healing". The game's death report
	// named the player for kills the followers made.
	void TestKillCreditFollowsTheFinalBlow()
	{
		Battle b;
		b.startedAt = 0;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		b.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
		b.Join(Info(kBandit1, "Cave Bear", Side::kEnemy, 300));
		b.Join(Info(kBandit2, "Cave Bear", Side::kEnemy, 300));
		b.Join(Info(kMage, "Cultist", Side::kEnemy, 100));
		b.Damage(kLydia, kBandit1, 60, 0.8f, 1);
		b.Heal(kLydia, kHero, 60, "Healing Hands", 2);
		b.Damage(kBandit1, kLydia, 300, 0.0f, 3);
		b.Death(kBandit1, kHero, 3.1);          // the game says the player did it
		b.Damage(kBandit2, kLydia, 300, 0.0f, 5);
		b.Death(kBandit2, kHero, 5.1);
		b.Damage(kMage, kLydia, 40, 0.6f, 6);
		b.Death(kMage, kHero, 20);              // no blow near the death, and the one the game names never struck them
		b.Join(Info(kChief, "Giant", Side::kEnemy, 600));
		b.Damage(kChief, kHero, 30, 0.95f, 21);
		b.Death(kChief, kHero, 30);             // no blow near the death, but the one named did strike them: the game's word stands
		b.Join(Info(kGuard, "Uthgerd", Side::kPlayer, 180));
		b.Damage(kGuard, kChief, 180, 0.0f, 22);
		b.Death(kGuard, kHero, 26);             // felled by the giant, reported late and against the player
		b.endedAt = 31;
		Check(b.Find(kLydia)->kills.size() == 3 && b.Find(kHero)->kills.size() == 1, "the one who dealt the final blow made the kill", "");
		const auto hero = SummaryText(BuildSummary(b, kHero, "Kaira", {}));
		Check(Has(hero, "Who killed the enemies: Lydia 3 (Cave Bear x2 and Cultist); Kaira 1 (Giant)."), "tally by final blow", hero);
		Check(Has(hero, "Uthgerd was killed by Giant.") && !Has(hero, "by accident"), "nobody is accused of killing an ally they never struck", hero);
		Check(Has(hero, "Kaira took no damage worth the name and dealt only a small part of their side's damage."), "a healer is not told as the one who did the fighting", hero);
	}

	// Injury mods (Blade and Blunt, Wildcat) and dismemberment (Dismembering Framework, the game's own beheading).
	void TestInjuriesAndDismemberment()
	{
		Battle b;
		b.startedAt = 0;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		b.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
		b.Join(Info(kChief, "Bandit Chief", Side::kEnemy, 400));
		b.Join(Info(kBandit1, "Bandit", Side::kEnemy, 100));
		b.Join(Info(kBandit2, "Bandit", Side::kEnemy, 100));
		b.Injury(kHero, "Minor Injury", true, 0);  // had it before the fight
		b.Damage(kLydia, kChief, 60, 0.8f, 2);
		b.Injury(kLydia, "Leg Injury", false, 3);
		b.Injury(kLydia, "Leg Injury", false, 4);  // seen again: told once
		b.Damage(kHero, kBandit1, 20, 0.9f, 5);
		b.Injury(kHero, "Major Injury", false, 30);  // long after the last blow: nobody named
		b.Damage(kChief, kLydia, 400, 0.0f, 31);
		b.Death(kChief, kLydia, 31);
		b.Dismember(kChief, true);
		b.Damage(kBandit1, kHero, 100, 0.0f, 32);
		b.Death(kBandit1, kHero, 32);
		b.Dismember(kBandit1, false);
		b.Damage(kBandit2, kHero, 100, 0.0f, 33);
		b.Death(kBandit2, kHero, 33);
		b.Dismember(kBandit2, false);
		b.Dismember(kLydia, true);  // the living are not dismembered
		b.endedAt = 40;

		const auto lydia = SummaryText(BuildSummary(b, kLydia, "Lydia", {}));
		Check(Has(lydia, "Lydia was injured in this fight and still carries it: Leg Injury (dealt by Bandit Chief)."), "a new injury, once, with its dealer", lydia);
		Check(Has(lydia, "Lydia beheaded Bandit Chief."), "a beheading is credited to the killer", lydia);
		Check(Has(lydia, "Kaira dismembered Bandit x2, severing a limb."), "severed limbs, grouped per killer", lydia);
		Check(Has(lydia, "Kaira: took only light wounds; dealt a fair share of their side's damage; was never near death; was injured (Major Injury); killed 2 of the other side."), "a companion's injury", lydia);
		Check(!b.Find(kLydia)->dismembered, "only the dead are dismembered", lydia);

		const auto hero = SummaryText(BuildSummary(b, kHero, "Kaira", {}));
		Check(Has(hero, "Kaira went into this fight already carrying: Minor Injury."), "an injury brought into the fight", hero);
		Check(Has(hero, "Kaira was injured in this fight and still carries it: Major Injury."), "an injury with no blow near it names nobody", hero);
		Check(!Has(hero, "None of them was seriously hurt"), "an injury makes it more than an easy fight", hero);

		const auto memory = BuildMemory(b, {});
		Check(Has(memory, "Lydia came out of it injured: Leg Injury (dealt by Bandit Chief).") && Has(memory, "Lydia beheaded Bandit Chief."), "both are remembered", memory);
		Check(Significant(b, 9, 900.0), "an injury is worth remembering", memory);
	}

	// Asked for by the author after a brawl in Whiterun in which a preacher was cut down and counted as an enemy.
	void TestInnocentsAndAllies()
	{
		Battle b;
		b.startedAt = 0;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		b.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
		b.Join(Info(kGuard, "Whiterun Guard", Side::kEnemy, 250));
		b.Join(Info(kBandit1, "Heimskr", Side::kEnemy, 75));  // the game calls him hostile once he has been struck
		b.Join(Info(kBandit2, "Whiterun Guard", Side::kEnemy, 250));
		b.Join(Info(kMage, "Bandit", Side::kEnemy, 100));
		for (const auto id : { kGuard, kBandit1, kBandit2 }) b.Find(id)->info.lawful = true;
		b.Armed(kGuard);
		b.Damage(kLydia, kGuard, 30, 0.9f, 1);
		b.Damage(kGuard, kHero, 250, 0.0f, 2);
		b.Death(kGuard, kHero, 2);
		b.Damage(kBandit1, kHero, 75, 0.0f, 3);
		b.Death(kBandit1, kHero, 3);
		b.Armed(kBandit2);  // swung and missed: a fighter all the same
		// a hostile who had turned on the party but was cut down before his sword was out (reported by a player: a
		// Dawnguard told as an innocent)
		b.Join(Info(kAtronach, "Dawnguard", Side::kEnemy, 150));
		b.Find(kAtronach)->info.lawful = true;
		b.CameToFight(kAtronach);
		b.Damage(kAtronach, kHero, 150, 0.0f, 3.5);
		b.Death(kAtronach, kHero, 3.5);
		b.Damage(kBandit2, kLydia, 250, 0.0f, 4);
		b.Death(kBandit2, kLydia, 4);
		b.Damage(kMage, kHero, 100, 0.0f, 5);  // never drew, but no hold's law covers a bandit
		b.Death(kMage, kHero, 5);
		b.Damage(kLydia, kHero, 300, 0.0f, 6);
		b.Death(kLydia, kHero, 6);
		b.endedAt = 10;

		const auto hero = SummaryText(BuildSummary(b, kHero, "Kaira", {}));
		Check(Has(hero, "Heimskr, who was not actively engaged in combat, was killed by Kaira."), "the death of someone who was not fighting is said outright", hero);
		Check(Has(hero, "Kaira and Lydia fought against Whiterun Guard x2, Bandit and Dawnguard.") && Has(hero, "All 4 enemies were killed.") && !Has(hero, "Dawnguard, who was not"), "someone who was not fighting is not one of the enemies", hero);
		Check(Has(hero, "Also caught up in it: Heimskr."), "but was caught up in it", hero);
		Check(Has(hero, "Lydia was killed by Kaira, by accident: they were on the same side."), "an ally killed by their own side", hero);
		Check(Has(hero, "Kaira killed 3 of the 4 who died on the other side"), "and is not one of the killer's enemy kills", hero);

		Battle m;  // nothing but a murder is still something to tell
		m.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		m.Join(Info(kBandit1, "Heimskr", Side::kOther, 75));
		m.Find(kBandit1)->info.lawful = true;
		m.Damage(kBandit1, kHero, 75, 0.0f, 1);
		m.Death(kBandit1, kHero, 1);
		m.endedAt = 2;
		Check(Significant(m, 3, 30.0) && Has(BuildMemory(m, {}), "Heimskr, who was not actively engaged in combat, was killed by Kaira."), "a murder is remembered", BuildMemory(m, {}));
	}

	// Seen in a brawl of spawned monsters in Windhelm: a monster that killed the one that had downed a townsman was
	// told as having saved his life, and a death at the hands of something the battle never saw as "killed by someone".
	void TestFoesSaveNobody()
	{
		Battle b;
		b.startedAt = 0;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		b.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
		b.Join(Info(kGuard, "Ursine", Side::kPlayer, 100));
		b.Join(Info(kChief, "Dwarven Centurion", Side::kEnemy, 400));
		b.Join(Info(kMage, "Sinmur", Side::kEnemy, 900));
		b.Damage(kHero, kMage, 10, 0.95f, 1);
		b.Damage(kLydia, kChief, 290, 0.03f, 2);
		b.Down(kLydia, 2);
		b.Damage(kChief, kMage, 400, 0.0f, 5);
		b.Death(kChief, kMage, 5);
		b.Death(kGuard, 0xABCDEF, 6);  // killed by something that never joined
		b.endedAt = 10;
		const auto lydia = SummaryText(BuildSummary(b, kLydia, "Lydia", {}));
		Check(Has(lydia, "Dwarven Centurion, who had struck down Lydia, was then killed by Sinmur.") && !Has(lydia, "saved Lydia"), "a foe's kill is told, but is no rescue", lydia);
		Check(Has(lydia, "Ursine was killed, by whom is not known.") && !Has(lydia, "someone"), "an unseen killer is not 'someone'", lydia);
		Check(Has(BuildMemory(b, {}), "Lydia was struck down by Dwarven Centurion: collapsed and helpless for a moment, but got back up."), "and it is remembered so", BuildMemory(b, {}));
	}

	// Asked for by a player whose followers kept complaining of being "drained and roasted" by the player's area spells.
	void TestFriendlyFire()
	{
		Battle b;
		b.startedAt = 0;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		b.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
		b.Join(Info(kBandit1, "Bandit", Side::kEnemy, 100));
		b.Damage(kLydia, kBandit1, 30, 0.9f, 1);
		b.Effect(kLydia, kBandit1, "poisoned", "Weak Poison", true);
		b.Effect(kLydia, kHero, "burned by fire", "Fireball", true);
		b.Damage(kLydia, kHero, 240, 0.1f, 2);  // the fireball takes her to the brink
		b.Damage(kBandit1, kHero, 100, 0.0f, 3);
		b.Death(kBandit1, kHero, 3);
		b.endedAt = 10;

		const auto told = SummaryText(BuildSummary(b, kLydia, "Lydia", {}));
		Check(Has(told, "Lydia suffered: poisoned by Bandit (Weak Poison).") && Has(told, "Lydia was caught in their own side's attacks, by accident (friendly fire, not an attack on them): took part of their damage from Kaira; burned by fire by Kaira (Fireball)."),
			"friendly fire is told apart, as an accident", told);
		Check(Has(told, "brought to the very edge of death by Kaira (by accident: friendly fire)"), "brought low by an ally: an accident", told);
		Check(Has(told, "Lydia was badly hurt") && !Has(told, "Part of the damage"), "damage from an ally is named as an accident", told);
		NarrativeOptions numbers;
		numbers.showNumbers = true;
		const auto hero = SummaryText(BuildSummary(b, kHero, "Kaira", numbers));
		Check(Has(hero, "dealt most of their side's damage (about 100 points)."), "what was done to an ally is not counted as fighting", hero);

		NarrativeOptions quiet;
		quiet.ignoreFriendlyFire = true;
		const auto hidden = SummaryText(BuildSummary(b, kLydia, "Lydia", quiet));
		Check(!Has(hidden, "Fireball") && !Has(hidden, "friendly fire") && !Has(hidden, "edge of death by Kaira"), "friendly fire left out", hidden);
		Check(Has(hidden, "Lydia took only light wounds") && !Has(hidden, "Part of the damage"), "and their damage is no part of the wounds told", hidden);
		Check(Has(hidden, "Lydia suffered: poisoned by Bandit (Weak Poison).") && Has(hidden, "Lydia was brought to the very edge of death, with almost no health left, but survived."), "the rest still told", hidden);
		Check(!Has(BuildMemory(b, quiet), "Kaira (by accident"), "and left out of what is remembered", BuildMemory(b, quiet));
	}

	// Reported by a player: "Ignore friendly fire" worked for followers but not for anyone else, and a bystander caught
	// in the crossfire of a fight with bandits was told as one of the enemies.
	void TestFriendlyFireOnBystanders()
	{
		Battle b;
		b.startedAt = 0;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		b.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
		b.Join(Info(kBandit1, "Bandit", Side::kEnemy, 100));
		b.Join(Info(kChief, "Bandit Chief", Side::kEnemy, 300));
		b.Join(Info(kGuard, "Whiterun Guard", Side::kOther, 250));
		b.Join(Info(kMage, "Hunter", Side::kOther, 100));        // stood by: no hold's law, never fought
		b.Join(Info(kAtronach, "Heimskr", Side::kOther, 75));
		b.Find(kGuard)->info.lawful = b.Find(kAtronach)->info.lawful = true;
		b.Damage(kLydia, kBandit1, 30, 0.9f, 1);
		b.Damage(kChief, kGuard, 60, 0.8f, 1.5);                 // the guard fights the bandits...
		b.Effect(kGuard, kHero, "burned by fire", "Fireball", true, 2);
		b.Damage(kGuard, kHero, 50, 0.8f, 2);                     // ...and is caught in the player's fireball
		b.Effect(kMage, kHero, Text::Get("fx.drained_life"), "", true, 2, Damaging(true));
		b.Damage(kMage, kHero, 40, 0.6f, 2);                      // so is the hunter, who fought nobody
		b.Effect(kLydia, kHero, "burned by fire", "Fireball", true, 2);
		b.Damage(kLydia, kHero, 40, 0.75f, 2);
		b.Effect(kBandit1, kHero, "burned by fire", "Fireball", true, 2);
		b.Damage(kBandit1, kHero, 100, 0.0f, 2);
		b.Death(kBandit1, kHero, 2);
		b.Damage(kAtronach, kHero, 75, 0.0f, 2.2);
		b.Death(kAtronach, kHero, 2.2);
		b.Damage(kChief, kLydia, 300, 0.0f, 6);
		b.Death(kChief, kLydia, 6);
		b.endedAt = 10;

		Check(b.EffectiveSide(*b.Find(kMage)) == Side::kOther, "a bystander struck only by the player's side is not an enemy", "");
		Check(b.EffectiveSide(*b.Find(kGuard)) == Side::kAlly, "a guard who fought the bandits is an ally", "");
		const auto hero = SummaryText(BuildSummary(b, kHero, "Kaira", {}));
		Check(Has(hero, "Kaira and Lydia fought against Bandit and Bandit Chief.") && Has(hero, "Whiterun Guard also fought them, apart from the party.")
				&& Has(hero, "Also caught up in it: Hunter and Heimskr."),
			"the bystanders are caught up in it, not enemies; the guard an ally", hero);

		NarrativeOptions quiet;
		quiet.ignoreFriendlyFire = true;
		const auto hunterTold = SummaryText(BuildSummary(b, kMage, "Hunter", {}));
		Check(Has(hunterTold, "Hunter was caught in the crossfire, by accident (not an attack on them): took part of their damage from Kaira; drained of life by Kaira.")
				&& !Has(hunterTold, "own side") && !Has(hunterTold, "Hunter suffered"),
			"a bystander hit by the player's side: told as an accident", hunterTold);
		const auto hunterHidden = SummaryText(BuildSummary(b, kMage, "Hunter", quiet));
		Check(!Has(hunterHidden, "from Kaira") && !Has(hunterHidden, "drained") && !Has(hunterHidden, "friendly fire") && Has(hunterHidden, "Hunter took no damage worth the name"),
			"a bystander hit by the player's side: left out", hunterHidden);

		const auto bandit = SummaryText(BuildSummary(b, kBandit1, "Bandit", quiet));
		Check(Has(bandit, "Bandit suffered: burned by fire by Kaira (Fireball).") && Has(bandit, "Bandit was killed by Kaira."), "what the player did to an enemy is still told", bandit);

		const auto guardTold = SummaryText(BuildSummary(b, kGuard, "Whiterun Guard", {}));
		Check(Has(guardTold, "Whiterun Guard was caught in the crossfire, by accident (not an attack on them): took part of their damage from Kaira; burned by fire by Kaira (Fireball).")
				&& !Has(guardTold, "dealt all the damage"),
			"a guard who fought beside the party: crossfire, and their part of all the damage on the enemy", guardTold);
		const auto guardHidden = SummaryText(BuildSummary(b, kGuard, "Whiterun Guard", quiet));
		Check(!Has(guardHidden, "Fireball") && !Has(guardHidden, "from Kaira"), "and left out as before", guardHidden);

		const auto lydiaTold = SummaryText(BuildSummary(b, kLydia, "Lydia", {}));
		const auto lydiaHidden = SummaryText(BuildSummary(b, kLydia, "Lydia", quiet));
		Check(Has(lydiaTold, "Lydia was caught in their own side's attacks") && !Has(lydiaHidden, "Fireball"), "a follower: unchanged", lydiaTold + lydiaHidden);

		// a death by friendly fire is always told
		Check(Has(SummaryText(BuildSummary(b, kAtronach, "Heimskr", quiet)), "Heimskr was killed by Kaira.")
				&& Has(BuildMemory(b, quiet), "Heimskr, who was not actively engaged in combat, was killed by Kaira."),
			"a bystander's death is told, the option or not", BuildMemory(b, quiet));

		// one only the player's enemies went for is still counted with the player's side
		Battle c;
		c.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		c.Join(Info(kBandit1, "Bandit", Side::kEnemy, 100));
		c.Join(Info(kMage, "Hunter", Side::kOther, 100));
		c.Damage(kHero, kBandit1, 10, 0.95f, 1);
		c.Damage(kMage, kBandit1, 30, 0.7f, 2);
		Check(c.EffectiveSide(*c.Find(kMage)) == Side::kAlly, "struck by the player's enemies: an ally", "");

		// with no enemies about, whoever the party went for is no bystander: a deer hunt is still a fight with the deer...
		Battle d;
		d.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		d.Join(Info(0x846F6, "Deer", Side::kOther, 50));
		d.Damage(0x846F6, kHero, 50, 0.0f, 1);
		d.Death(0x846F6, kHero, 1);
		d.endedAt = 5;
		const auto hunt = SummaryText(BuildSummary(d, kHero, "Kaira", quiet));
		Check(d.EffectiveSide(*d.Find(0x846F6)) == Side::kEnemy && d.Count(Side::kEnemy) == 1 && Has(hunt, "Kaira fought against Deer.") && Has(hunt, "Who killed the enemies: Kaira 1 (Deer)."),
			"a deer hunt is a battle with the deer, as before", hunt);

		// ...and a townsman attacked on his own is told as attacked, the option or not
		Battle t;
		t.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		t.Join(Info(kAtronach, "Heimskr", Side::kOther, 75));
		t.Join(Info(kMage, "Farmhand", Side::kOther, 80));
		t.Find(kAtronach)->info.lawful = true;
		t.Effect(kAtronach, kHero, "burned by fire", "Flames", true, 1);
		t.Damage(kAtronach, kHero, 40, 0.45f, 1);
		t.Damage(kMage, kHero, 30, 0.6f, 2);  // no hold's law
		t.endedAt = 5;
		Check(t.EffectiveSide(*t.Find(kMage)) == Side::kEnemy, "someone else attacked on their own is the one the player fought, as before", "");
		for (const bool ignore : { false, true }) {
			NarrativeOptions o;
			o.ignoreFriendlyFire = ignore;
			const auto assault = SummaryText(BuildSummary(t, kAtronach, "Heimskr", o));
			Check(Has(assault, "Heimskr suffered: burned by fire by Kaira (Flames).") && !Has(assault, "by accident") && !Has(assault, "crossfire") && Has(assault, "took solid wounds"),
				"a lone assault on a townsman is told as an attack", assault);
		}
	}

	// Seen in a player's log: something on the player pulsed an Absorb Health effect onto everyone nearby every half
	// second, damaging none of them, and each was told the player had "drained their life". A damage effect is told
	// only when it took health; what does no damage (paralysis, a slow from a spell without damage) is told as before.
	void TestHarmlessEffects()
	{
		constexpr std::uint32_t kDrainSpell = 0xFF00A001, kFrostbite = 0xFF00A002, kParalyze = 0xFF00A003;
		const auto drained = Text::Get("fx.drained_life"), frozen = Text::Get("fx.frozen"), slowed = Text::Get("fx.slowed"), paralyzed = Text::Get("fx.paralyzed");
		const auto replay = [&](bool a_ignoreHarmless) {
			Battle b;
			b.startedAt = 0;
			b.ignoreHarmless = a_ignoreHarmless;
			b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
			b.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
			b.Join(Info(kBandit1, "Bandit", Side::kEnemy, 100));
			b.Join(Info(kMage, "Bandit Mage", Side::kEnemy, 80));
			b.Join(Info(kGuard, "Hunter", Side::kOther, 100));
			for (double t = 1.0; t <= 6.0; t += 0.5) {  // the pulse, on everyone in reach, doing nothing to any of them
				for (const auto id : { kLydia, kBandit1, kGuard }) b.Effect(id, kHero, drained, "", true, t, Damaging(false));
			}
			b.Damage(kBandit1, kHero, 30, 0.7f, 2.0);  // the player's sword, while the pulse was on him
			b.Effect(kLydia, kMage, drained, "Vampiric Drain", true, 3.0, Damaging(true, kDrainSpell));
			b.Damage(kLydia, kMage, 20, 0.9f, 3.2);  // what it took
			b.Effect(kHero, kMage, frozen, "Frostbite", true, 4.0, Damaging(true, kFrostbite, 1.0));
			b.Effect(kHero, kMage, slowed, "Frostbite", true, 4.0, { kFrostbite });
			b.Damage(kHero, kMage, 15, 0.9f, 4.6);
			b.Effect(kLydia, kBandit1, paralyzed, "Paralyze", true, 5.0, { kParalyze });  // no damage in that spell
			b.Effect(kLydia, kMage, frozen, "Frostbite", true, 6.0, Damaging(false, kFrostbite, 1.0));  // a frostbite that did nothing...
			b.Effect(kLydia, kMage, slowed, "Frostbite", true, 6.0, { kFrostbite });                    // ...and its slow
			b.Damage(kBandit1, kLydia, 70, 0.0f, 7);
			b.Death(kBandit1, kLydia, 7);
			b.endedAt = 10;
			return b;
		};

		const auto b = replay(true);
		Check(!b.Involved(*b.Find(kGuard)), "a damage effect that took nothing draws nobody into the battle", "");
		const auto hero = SummaryText(BuildSummary(b, kHero, "Kaira", {}));
		Check(!Has(hero, "Hunter"), "and they are not told as caught up in it", hero);
		Check(Has(hero, "Kaira suffered: frozen by frost by Bandit Mage (Frostbite) and slowed by Bandit Mage (Frostbite)."), "a frost spell that took health: its slow goes with it", hero);
		const auto lydia = SummaryText(BuildSummary(b, kLydia, "Lydia", {}));
		Check(Has(lydia, "Lydia suffered: drained of life by Bandit Mage (Vampiric Drain) and paralyzed by Bandit (Paralyze)."), "a drain that took health is told; paralysis without damage too", lydia);
		Check(!Has(lydia, "frozen") && !Has(lydia, "slowed") && !Has(lydia, "by Kaira") && !Has(lydia, "caught in"),
			"a frost spell that did nothing, its slow, and an empty drain from her own side are not", lydia);
		const auto bandit = SummaryText(BuildSummary(b, kBandit1, "Bandit", {}));
		Check(!Has(bandit, "drained"), "an empty drain on an enemy the player struck is not told", bandit);
		const auto hunter = BuildSummary(b, kGuard, "Hunter", {});
		Check(!hunter.participant && !Has(SummaryText(hunter), "drained"), "a bystander it reached is told nothing about it", SummaryText(hunter));

		const auto all = replay(false);  // as 1.3.0: every damage effect told when it lands
		Check(Has(SummaryText(BuildSummary(all, kBandit1, "Bandit", {})), "Bandit suffered: drained of life by Kaira."), "with the setting off, the drains are told", "");
		Check(all.Involved(*all.Find(kGuard)) && Has(SummaryText(BuildSummary(all, kGuard, "Hunter", {})), "drained of life by Kaira"), "on bystanders too", "");
		Check(Has(SummaryText(BuildSummary(all, kLydia, "Lydia", {})), "frozen by frost by Bandit Mage (Frostbite)"), "and the frost spell that did nothing", "");
	}

	// The first of the "significant moments": several foes killed by one fighter in the same instant.
	void TestKilledAtOnce()
	{
		Battle b;
		b.startedAt = 0;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		b.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
		b.Join(Info(kBandit1, "Bandit", Side::kEnemy, 100));
		b.Join(Info(kBandit2, "Bandit", Side::kEnemy, 100));
		b.Join(Info(kChief, "Bandit Chief", Side::kEnemy, 300));
		b.Join(Info(kMage, "Bandit Mage", Side::kEnemy, 80));
		b.Join(Info(kGuard, "Bandit Archer", Side::kEnemy, 80));
		b.Damage(kHero, kChief, 20, 0.9f, 1);
		// a fireball: three die within the same half second
		for (const auto id : { kBandit1, kBandit2, kChief }) {
			b.Effect(id, kHero, "burned by fire", "Fireball", true, 4.9);
			b.Damage(id, kHero, 400, 0.0f, 5.0);
		}
		b.Death(kBandit1, kHero, 5.0);
		b.Death(kBandit2, kHero, 5.1);
		b.Death(kChief, kHero, 5.4);
		for (const auto id : { kBandit1, kBandit2, kChief }) b.Find(id)->killedWith = b.RecentSpell(id, kHero, 5.4, 1.5);
		// Lydia's two kills are seconds apart: not at once
		b.Damage(kMage, kLydia, 80, 0.0f, 8);
		b.Death(kMage, kLydia, 8);
		b.Damage(kGuard, kLydia, 80, 0.0f, 11);
		b.Death(kGuard, kLydia, 11);
		b.endedAt = 15;
		const auto told = SummaryText(BuildSummary(b, kLydia, "Lydia", {}));
		Check(Has(told, "Kaira killed 3 at once with Fireball: Bandit x2 and Bandit Chief."), "three in one stroke, and with what", told);
		Check(!Has(told, "Lydia killed 2 at once"), "kills seconds apart are not at once", told);
		b.Find(kChief)->killedWith = "Steel Greatsword";  // not all by the same means: the means is not told
		Check(Has(SummaryText(BuildSummary(b, kLydia, "Lydia", {})), "Kaira killed 3 at once: Bandit x2 and Bandit Chief."), "mixed means are left unsaid", told);
		b.Find(kChief)->killedWith = "Fireball";
		Check(Has(BuildMemory(b, {}), "Kaira killed 3 at once with Fireball: Bandit x2 and Bandit Chief."), "and it is remembered", BuildMemory(b, {}));
	}

	// Every phrase can be reworded by the player; nothing they type may blank a summary.
	void TestOwnWording()
	{
		std::set<std::string_view> keys;
		bool                       tidy = true;
		for (const auto& e : Text::Defaults()) {
			tidy = tidy && keys.insert(e.key).second && !e.text.empty() && e.text.front() != ' ' && e.text.back() != ' ' && Text::Accepts(e.key, e.text);
		}
		Check(tidy && keys.size() > 140, "every phrase has its own key and survives an ini file (no spaces at either end)", std::to_string(keys.size()));

		std::string why;
		Check(!Text::Accepts("no.such.phrase", "anything", &why) && why == "there is no phrase of that name", "a line for no phrase is refused", why);
		Check(!Text::Accepts("killed.not_fighting", "{name}, an innocent, was murdered by {murderer}.", &why) && Has(why, "{murderer}"), "a placeholder the phrase lacks is refused", why);
		Check(Text::Accepts("killed.not_fighting", "An innocent died."), "a placeholder may be left out", "");

		Battle m;
		m.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		m.Join(Info(kBandit1, "Heimskr", Side::kOther, 75));
		m.Find(kBandit1)->info.lawful = true;
		m.Damage(kBandit1, kHero, 75, 0.0f, 1);
		m.Death(kBandit1, kHero, 1);
		m.endedAt = 2;
		const auto before = BuildMemory(m, {});

		const auto taken = Text::SetOverrides({
			{ "killed.not_fighting", "{name}, an innocent, {killed}." },
			{ "killed.by", "was cut down by {killer}" },
			{ "battle.fought", "Blood was spilled{at_place}." },            // {duration} left out
			{ "wounds.light", "{name} barely bled" },                        // refused: no {name} in that phrase
			{ "no.such.phrase", "ignored" },
			{ "dealt.none", "" },                                            // empty: the built-in stands
		});
		const auto own = BuildMemory(m, {});
		Check(taken == 3, "the lines that can be used are taken, the rest left out", std::to_string(taken));
		Check(Has(own, "Blood was spilled.") && Has(own, "Heimskr, an innocent, was cut down by Kaira."), "the player's wording is told", own);
		Check(Text::Get("wounds.light") == "took only light wounds" && Text::Get("dealt.none") == "dealt no damage to anyone",
			"a refused or empty line falls back to the built-in wording", Text::Get("wounds.light") + " / " + Text::Get("dealt.none"));

		Text::SetOverrides({});
		Check(BuildMemory(m, {}) == before, "and without the file the built-in wording is back", BuildMemory(m, {}));

		const auto ref = Text::Reference();
		Check(Has(ref, "\n[Deaths]\n") && Has(ref, "killed.not_fighting = {name}, who was not actively engaged in combat, {killed}.") && Has(ref, "fx.burned = burned by fire"),
			"the reference file lists every phrase under its heading", ref.substr(0, 400));
	}

	// Seen in play: three wolves of one name each left the same effect, told three times over; and a bite read as
	// "harmful magic" under a name never meant to be read.
	void TestSayLessWhenInDoubt()
	{
		Battle b;
		b.startedAt = 0;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		for (const auto id : { kBandit1, kBandit2, kChief }) {
			b.Join(Info(id, "Caged Wolf", Side::kEnemy, 60));
			b.Damage(kHero, id, 10, 0.9f, 1);
			b.Effect(kHero, id, Text::Get("fx.hurt"), "", true, 1);  // the tracker leaves an unknowable source unnamed
		}
		b.Effect(kHero, kBandit1, "burned by fire", "Flames", true, 2);
		b.endedAt = 5;
		const auto told = SummaryText(BuildSummary(b, kHero, "Kaira", {}));
		Check(Has(told, "Kaira suffered: hurt by a harmful effect by Caged Wolf and burned by fire by Caged Wolf (Flames)."), "the same thing from same-named attackers is told once, and nothing is guessed", told);
	}

	// A player's report: any line about someone killed without fighting set their followers (and other mods tracking
	// such things) against them. Left out by default; enemies killed unaware still count.
	void TestNonCombatantsLeftOut()
	{
		constexpr ActorId kDeer = 0xFF000901, kVigilant = 0xFF000902;
		const auto fight = [&](bool a_omit) {
			Battle b;
			b.omitBystanders = a_omit;
			b.startedAt = 0;
			b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
			b.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
			b.Join(Info(kBandit1, "Bandit", Side::kEnemy, 100));
			b.Join(Info(kBandit2, "Heimskr", Side::kOther, 75));
			b.Find(kBandit2)->info.lawful = true;
			b.Join(Info(kDeer, "Deer", Side::kOther, 40));
			b.Join(Info(kVigilant, "Vigilant of Stendarr", Side::kOther, 150));
			b.Find(kVigilant)->info.lawful = true;
			b.Find(kVigilant)->info.wouldFight = true;  // his order hunts vampires: an enemy, asleep or not
			b.Damage(kLydia, kBandit1, 30, 0.9f, 1);
			b.Damage(kBandit1, kLydia, 100, 0.0f, 2);
			b.Death(kBandit1, kLydia, 2);
			b.Damage(kBandit2, kHero, 75, 0.0f, 3);  // a townsman cut down in the fight
			b.Death(kBandit2, kHero, 3);
			b.Damage(kDeer, kHero, 40, 0.0f, 4);
			b.Death(kDeer, kHero, 4);
			b.Damage(kVigilant, kHero, 150, 0.0f, 5);  // a sneak attack: he never drew
			b.Death(kVigilant, kHero, 5);
			b.endedAt = 10;
			return b;
		};
		const auto left = fight(true);
		const auto told = SummaryText(BuildSummary(left, kLydia, "Lydia", {}));
		Check(!Has(told, "Heimskr") && !Has(told, "Deer") && !Has(told, "caught up"), "non-combatants are left out altogether", told);
		Check(Has(told, "Kaira and Lydia fought against Bandit and Vigilant of Stendarr.") && Has(told, "Both enemies were killed."),
			"an enemy killed unaware still counts", told);
		Check(Has(told, "Who killed the enemies: Lydia 1 (Bandit); Kaira 1 (Vigilant of Stendarr)."), "and is credited", told);
		Check(!Has(BuildMemory(left, {}), "Heimskr"), "and nothing of them is remembered", BuildMemory(left, {}));

		const auto mentioned = SummaryText(BuildSummary(fight(false), kLydia, "Lydia", {}));
		Check(Has(mentioned, "Heimskr, who was not actively engaged in combat, was killed by Kaira."), "with the setting on, the old line", mentioned);
		Check(!Has(mentioned, "Vigilant of Stendarr, who was not"), "and an enemy killed unaware is never one of them", mentioned);

		Battle only;  // a townsman's murder and nothing else: no battle to tell, with them left out
		only.omitBystanders = true;
		only.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		only.Join(Info(kBandit2, "Heimskr", Side::kOther, 75));
		only.Find(kBandit2)->info.lawful = true;
		only.Damage(kBandit2, kHero, 75, 0.0f, 1);
		only.Death(kBandit2, kHero, 1);
		only.endedAt = 2;
		Check(only.Count(Side::kEnemy) == 0 && !Significant(only, 3, 30.0), "a murder alone is no battle to remember", BuildMemory(only, {}));
	}

	// Reported by players: a companion's mechanical spider, re-summoned all through a long fight, was told as killed 18
	// times; followers mourned a conjured skeleton.
	void TestSummons()
	{
		constexpr ActorId kRemiel = 0xFF000911, kScrap1 = 0xFF000912, kScrap2 = 0xFF000913, kScrap3 = 0xFF000914, kWolf1 = 0xFF000915, kWolf2 = 0xFF000916;
		Battle b;
		b.omitBystanders = true;
		b.startedAt = 0;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		b.Join(Info(kRemiel, "Remiel", Side::kPlayer, 250));
		for (const auto id : { kScrap1, kScrap2, kScrap3 }) {
			b.Join(Info(id, "Scrap", Side::kPlayer, 120, kRemiel));
			b.Find(id)->info.summon = true;
		}
		b.Join(Info(kWolf1, "Wolf", Side::kEnemy, 60));
		b.Join(Info(kWolf2, "Wolf", Side::kEnemy, 60));
		for (const auto id : { kWolf1, kWolf2 }) b.Find(id)->info.wouldFight = true;  // wolves attack on sight
		b.Damage(kWolf1, kScrap1, 60, 0.0f, 1);
		b.Death(kWolf1, kScrap1, 1);
		b.Damage(kScrap1, kWolf2, 120, 0.0f, 2);
		b.Death(kScrap1, kWolf2, 2);
		b.Death(kScrap2, 0, 5);  // dismissed: no blow, no killer
		b.Damage(kWolf2, kScrap3, 60, 0.0f, 6);
		b.Death(kWolf2, kScrap3, 6);
		b.endedAt = 10;
		// a corpse raised after it died is no summon: its first death stays a kill
		b.Join(Info(kWolf1, "Wolf", Side::kEnemy, 60, 0xFF000999));
		b.Find(kWolf1)->info.summon = false;
		ActorInfo raised = Info(kWolf1, "Wolf", Side::kEnemy, 60, 0xFF000999);
		raised.summon = true;
		b.Join(raised);
		Check(!b.Find(kWolf1)->info.summon, "a summon is decided when it first joins", "");
		const auto remiel = SummaryText(BuildSummary(b, kRemiel, "Remiel", {}));
		Check(!Has(remiel, "Scrap was killed") && !Has(remiel, "Scrap:") && Has(remiel, "Kaira and Remiel fought against Wolf x2."),
			"a summon is not a fighter of its own, and its end is no death", remiel);
		Check(Has(remiel, "Who killed the enemies: Remiel 2 (Wolf x2, 2 through Scrap)."), "its kills are its master's, with its name", remiel);
		Check(Has(remiel, "Remiel killed 2 of the 2 who died on the other side: Wolf x2.") && Has(remiel, "2 of them through Scrap."), "told to the master too", remiel);
		Check(!Has(BuildMemory(b, {}), "Scrap was"), "and nothing is remembered of its ends", BuildMemory(b, {}));
	}

	// The enemy's summons are no enemies of their own, but putting an end to one is still the fighter's doing.
	void TestEnemySummonsDispatched()
	{
		constexpr ActorId kNecro = 0xFF000921, kAtro1 = 0xFF000922, kAtro2 = 0xFF000923, kAtro3 = 0xFF000924;
		Battle b;
		b.omitBystanders = true;
		b.startedAt = 0;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		b.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
		b.Join(Info(kNecro, "Necromancer", Side::kEnemy, 150));
		for (const auto id : { kAtro1, kAtro2, kAtro3 }) {
			b.Join(Info(id, "Flame Atronach", Side::kEnemy, 100, kNecro));
			b.Find(id)->info.summon = true;
			b.Find(id)->info.wouldFight = true;
		}
		b.Damage(kLydia, kAtro1, 40, 0.86f, 1);
		b.Damage(kAtro1, kLydia, 100, 0.0f, 2);
		b.Death(kAtro1, kLydia, 2);
		b.Damage(kAtro2, kHero, 100, 0.0f, 3);
		b.Death(kAtro2, kHero, 3);
		b.Damage(kAtro3, kHero, 30, 0.7f, 3.8);  // struck once, then it vanished with its summoner: still nobody's doing
		b.Death(kAtro3, 0, 4);
		b.Damage(kNecro, kHero, 150, 0.0f, 4);
		b.Death(kNecro, kHero, 4);
		b.endedAt = 8;
		const auto told = SummaryText(BuildSummary(b, kLydia, "Lydia", {}));
		Check(Has(told, "The one enemy was killed.") && Has(told, "Who killed the enemies: Kaira 1 (Necromancer)."), "the enemy proper, apart from its summons", told);
		Check(Has(told, "Summoned creatures dispatched: Lydia 1 (Flame Atronach); Kaira 1 (Flame Atronach)."), "who dispatched them", told);
		const auto kaira = SummaryText(BuildSummary(b, kHero, "Kaira", {}));
		Check(Has(told, "Lydia also dispatched Flame Atronach.") && Has(kaira, "Kaira killed 1 of the 1 who died on the other side: Necromancer.") &&
				  Has(kaira, "Kaira also dispatched Flame Atronach."),
			"told to each who did", kaira);
	}

	// Seen in play: two Trolls, one that struck a follower down and one that had wounded her, read as one Troll killed twice.
	void TestSameNames()
	{
		constexpr ActorId kTroll1 = 0xFF000931, kTroll2 = 0xFF000932;
		Battle b;
		b.startedAt = 0;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		b.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
		b.Join(Info(kTroll1, "Troll", Side::kEnemy, 300));
		b.Join(Info(kTroll2, "Troll", Side::kEnemy, 300));
		b.Damage(kLydia, kTroll2, 150, 0.5f, 1);
		b.Damage(kLydia, kTroll1, 140, 0.03f, 2);
		b.Down(kLydia, 2);
		b.Damage(kTroll1, kHero, 300, 0.0f, 4);
		b.Death(kTroll1, kHero, 4);
		b.Up(kLydia);
		b.Damage(kTroll2, kLydia, 300, 0.0f, 8);
		b.Death(kTroll2, kLydia, 8);
		b.endedAt = 10;
		const auto lydia = SummaryText(BuildSummary(b, kLydia, "Lydia", {}));
		Check(Has(lydia, "Lydia was struck down by the first Troll") && Has(lydia, "The first Troll, who had struck down Lydia, was then killed by Kaira.") &&
				  Has(lydia, "Lydia recovered enough to kill the second Troll, who had also wounded Lydia badly."),
			"same-named fighters are numbered, and only the one that did it struck her down", lydia);
		Check(Has(lydia, "Kaira and Lydia fought against Troll x2."), "lists keep the name alone", lydia);
		const auto hero = SummaryText(BuildSummary(b, kHero, "Kaira", {}));
		Check(Has(hero, "Lydia: ") && !Has(hero, "- the "), "every line starts with a capital", hero);
		Check(Has(hero, "Kaira saved Lydia's life. The first Troll, who had struck down Lydia, was then killed by Kaira."), "and every sentence within one", hero);
	}

	// Players' reports: followers measured themselves by kills alone ("whoever deals the last 10 damage to the dragon
	// gets the glory"), and nobody knew who had drunk what.
	void TestDamageAndItems()
	{
		constexpr ActorId kDragon = 0xFF000941;
		Battle b;
		b.startedAt = 0;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		b.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
		b.Join(Info(kDragon, "Dragon", Side::kEnemy, 2000));
		b.Damage(kDragon, kLydia, 1990, 0.005f, 1);
		b.Damage(kDragon, kHero, 10, 0.0f, 2);
		b.Death(kDragon, kHero, 2);
		b.UsedItem(kHero, "Potion of Healing", false, 0.5);
		b.UsedItem(kHero, "Potion of Healing", false, 0.6);  // the same drink's other effect
		b.UsedItem(kHero, "Potion of Healing", false, 3.0);
		b.UsedItem(kHero, "Potion of Fortify Two-handed", false, 3.1);
		b.UsedItem(kLydia, "Deadly Venom", true, 1.0);
		b.endedAt = 5;
		const auto hero = SummaryText(BuildSummary(b, kHero, "Kaira", {}));
		Check(Has(hero, "Who killed the enemies: Kaira 1 (Dragon).") && Has(hero, "Most of the party's damage to the enemy was dealt by Lydia."),
			"the kill and the work, told apart", hero);
		Check(Has(hero, "Kaira drank 3 potions in the fight: Potion of Healing x2 and Potion of Fortify Two-handed."), "potions drunk, one drink counted once", hero);
		const auto lydia = SummaryText(BuildSummary(b, kLydia, "Lydia", {}));
		Check(Has(lydia, "Lydia took no damage worth the name and dealt most of their side's damage.") && Has(lydia, "Lydia used a poison in the fight: Deadly Venom."),
			"her own share and her poison", lydia);
		NarrativeOptions numbers;
		numbers.showNumbers = true;
		Check(Has(SummaryText(BuildSummary(b, kHero, "Kaira", numbers)), "dealt by Lydia (about 100%)"), "shares as figures only when asked for", "");
	}

	// The author's rule: credit for every part played over the line, the tank only for a real tank.
	void TestRoles()
	{
		constexpr ActorId kArcher = 0xFF000951, kMagus = 0xFF000952, kOgre = 0xFF000953, kOgre2 = 0xFF000954;
		Battle b;
		b.startedAt = 0;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		b.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
		b.Join(Info(kArcher, "Jenassa", Side::kPlayer, 150));
		b.Join(Info(kMagus, "Marcurio", Side::kPlayer, 150));
		b.Join(Info(kOgre, "Troll", Side::kEnemy, 900));
		b.Join(Info(kOgre2, "Troll", Side::kEnemy, 900));
		// Kaira, Lydia and Marcurio take the blows between them; Jenassa shoots from afar and takes none
		b.Damage(kHero, kOgre, 90, 0.55f, 1);
		b.Damage(kLydia, kOgre, 120, 0.6f, 2);
		b.Damage(kMagus, kOgre2, 70, 0.5f, 3);
		b.Heal(kLydia, kMagus, 100, "Heal Other", 4);
		b.Effect(kHero, kMagus, "shielded by magical armor", "Oakflesh", false, 4);
		b.Effect(kLydia, kMagus, "shielded by magical armor", "Oakflesh", false, 4);
		b.Effect(kOgre, kArcher, Text::Get("fx.paralyzed"), "Paralyze Poison", true, 5);
		b.Effect(kOgre2, kArcher, Text::Get("fx.slowed"), "Ice Arrow", true, 5);
		b.Damage(kOgre, kArcher, 500, 0.4f, 6);
		b.Damage(kOgre, kLydia, 400, 0.0f, 7);
		b.Death(kOgre, kLydia, 7);
		b.Damage(kOgre2, kHero, 700, 0.2f, 8);
		b.Damage(kOgre2, kArcher, 200, 0.0f, 9);
		b.Death(kOgre2, kArcher, 9);
		b.endedAt = 10;
		const auto told = SummaryText(BuildSummary(b, kArcher, "Jenassa", {}));
		Check(Has(told, "Most of the party's damage to the enemy was dealt by Kaira and Jenassa.") && Has(told, "Lydia also did a real part of it."), "the most, then those over the line", told);
		Check(Has(told, "Lydia, Kaira and Marcurio took the enemy's blows between them.") && !Has(told, "brunt"), "no real tank: shared, and the archer who took none is not named", told);
		Check(Has(told, "Marcurio kept Lydia going with healing.") && Has(told, "Marcurio shielded the others with magic."), "healing one other, shielding two", told);
		Check(Has(told, "Jenassa held the enemy back (paralyzed and slowed)."), "hindering the enemy", told);

		Battle t;  // a real tank
		t.startedAt = 0;
		t.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		t.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
		t.Join(Info(kArcher, "Jenassa", Side::kPlayer, 150));
		t.Join(Info(kOgre, "Troll", Side::kEnemy, 900));
		t.Damage(kLydia, kOgre, 250, 0.2f, 1);
		t.Damage(kHero, kOgre, 90, 0.55f, 2);
		t.Damage(kArcher, kOgre, 10, 0.9f, 3);
		t.Damage(kOgre, kHero, 900, 0.0f, 4);
		t.Death(kOgre, kHero, 4);
		t.endedAt = 5;
		Battle clean;  // the party took nothing: no share of blows for anyone, and that said plainly
		clean.startedAt = 0;
		clean.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		clean.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
		clean.Join(Info(kOgre, "Troll", Side::kEnemy, 900));
		clean.Damage(kOgre, kLydia, 450, 0.5f, 1);
		clean.Damage(kOgre, kHero, 450, 0.0f, 2);
		clean.Death(kOgre, kHero, 2);
		clean.endedAt = 3;
		const auto none = SummaryText(BuildSummary(clean, kLydia, "Lydia", {}));
		Check(Has(none, "The enemy never landed a blow on the party.") && !Has(none, "blows between them") && !Has(none, "brunt") && !Has(none, "share of the enemy"),
			"no damage taken: said plainly, and no share of blows for anyone", none);
		const auto tank = SummaryText(BuildSummary(t, kHero, "Kaira", {}));
		Check(Has(tank, "Lydia bore the brunt of the enemy's blows.") && Has(tank, "Kaira also took a share of the enemy's blows.") && !Has(tank, "Jenassa also took"),
			"over half: the brunt; a fifth or more: a share; less: nothing", tank);
	}

	// Shares are measured against an even split: what a fifth means in a party of four is not what it means in one of ten
	void TestEvenShares()
	{
		constexpr ActorId kBase = 0xFF000960, kFoe = 0xFF000970;
		const char*       names[] = { "Kaira", "Lydia", "Marcurio", "Aela", "Brelyna", "J'zargo", "Uthgerd", "Jenassa", "Erik", "Borgakh" };
		const auto        party = [&](Battle& a_b, int a_n) {
			a_b.startedAt = 0;
			for (int i = 0; i < a_n; ++i) a_b.Join(Info(i == 0 ? kHero : kBase + i, names[i], Side::kPlayer, 300));
		};
		const auto id = [&](int i) { return i == 0 ? kHero : kBase + i; };

		Battle even;  // ten who did the same: nobody singled out
		party(even, 10);
		for (int k = 0; k < 4; ++k) {
			even.Join(Info(kFoe + k, "Bandit", Side::kEnemy, 1000));
			for (int i = 0; i < 10; ++i) {
				even.Damage(id(i), kFoe + k, 30, 0.8f, k * 10 + i * 0.1);
				even.Damage(kFoe + k, id(i), 100, 1.0f - 0.1f * (i + 1), k * 10 + i * 0.1 + 0.05);
			}
			even.Death(kFoe + k, id(k), k * 10 + 1);
		}
		even.endedAt = 50;
		const auto e = SummaryText(BuildSummary(even, kBase + 1, "Lydia", {}));
		Check(Has(e, "The party shared the damage to the enemy evenly.") && !Has(e, "Most of the party's damage"), "an even fight: no false \"most\"", e);
		Check(Has(e, "The enemy's blows fell evenly across the party.") && !Has(e, "largest share"), "even blows: nobody took the largest share", e);
		Check(Has(e, "Lydia took solid wounds and dealt a fair share"), "a tenth of ten is a fair share, not a small part", e);

		Battle duo;  // a duo: more than the other is not yet the brunt
		party(duo, 2);
		duo.Join(Info(kFoe, "Troll", Side::kEnemy, 600));
		duo.Damage(kBase + 1, kFoe, 150, 0.5f, 1);
		duo.Damage(kHero, kFoe, 80, 0.75f, 2);
		duo.Damage(kFoe, kHero, 330, 0.45f, 3);
		duo.Damage(kFoe, kBase + 1, 270, 0.0f, 4);
		duo.Death(kFoe, kBase + 1, 4);
		duo.endedAt = 5;
		const auto d = SummaryText(BuildSummary(duo, kBase + 1, "Lydia", {}));
		Check(!Has(d, "brunt") && Has(d, "Lydia and Kaira took the enemy's blows between them."), "a duo at 65%: shared, not the brunt", d);
		Check(Has(d, "The party shared the damage to the enemy evenly.") && !Has(d, "dealt by Kaira and Lydia"), "a duo's damage line never names both", d);

		Battle pets;  // the atronach took the blows: told as Kaira's, not as Lydia's few scratches
		party(pets, 3);
		pets.Join(Info(kBase + 20, "Flame Atronach", Side::kPlayer, 300, kHero)).info.summon = true;
		pets.Join(Info(kFoe, "Troll", Side::kEnemy, 600));
		pets.Damage(kBase + 20, kFoe, 250, 0.2f, 1);
		pets.Damage(kBase + 1, kFoe, 40, 0.9f, 2);
		pets.Damage(kFoe, kBase + 20, 300, 0.5f, 3);
		pets.Damage(kFoe, kBase + 1, 300, 0.0f, 4);
		pets.Death(kFoe, kBase + 1, 4);
		pets.endedAt = 5;
		const auto t = SummaryText(BuildSummary(pets, kBase + 1, "Lydia", {}));
		Check(Has(t, "Kaira's Flame Atronach bore the brunt of the enemy's blows."), "a summon that drew the blows is told as its master's", t);

		Battle many;  // a long list of names is cut short
		party(many, 10);
		many.Join(Info(kFoe, "Giant", Side::kEnemy, 3000));
		for (int i = 0; i < 10; ++i) many.Damage(id(i), kFoe, i < 6 ? 60.0f : 5.0f, 0.8f, i * 0.2);
		many.Damage(kFoe, kHero, 1500, 0.0f, 3);
		many.Death(kFoe, kHero, 3);
		many.endedAt = 4;
		const auto m = SummaryText(BuildSummary(many, kBase + 1, "Lydia", {}));
		Check(Has(m, "took the enemy's blows between them") && Has(m, "and 3 others took the enemy's blows"), "six names over the line: three and \"3 others\"", m);
	}

	// The player was carried away mid-fight (a death-alternative mod, a teleport): the enemies left alive did not flee
	void TestCutShort()
	{
		Battle b;
		b.startedAt = 0;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 300));
		b.Join(Info(kBandit1, "Bandit", Side::kEnemy, 100));
		b.Join(Info(kBandit2, "Bandit", Side::kEnemy, 100));
		b.Damage(kBandit1, kHero, 100, 0.0f, 1);
		b.Death(kBandit1, kHero, 1);
		b.Damage(kHero, kBandit2, 300, 0.0f, 2);
		b.Down(kHero, 2);
		b.endedAt = 3;
		const auto plain = BuildMemory(b, {});
		Check(Has(plain, "1 of the 2 enemies were killed; the rest fled, yielded or broke off."), "ended where it was: the rest broke off", plain);
		b.cutShort = true;
		const auto cut = BuildMemory(b, {});
		Check(Has(cut, "The fight was cut short; by then 1 of the 2 enemies had been killed.") && !Has(cut, "fled"), "cut short: nobody is said to have fled", cut);
		Battle none;  // moved away before anyone fell
		none.startedAt = 0;
		none.Join(Info(kHero, "Kaira", Side::kPlayer, 300));
		none.Join(Info(kBandit1, "Bandit", Side::kEnemy, 100));
		none.Damage(kHero, kBandit1, 100, 0.6f, 1);
		none.endedAt = 2;
		none.cutShort = true;
		const auto early = BuildMemory(none, {});
		Check(Has(early, "The fight was cut short before the one enemy fell.") && !Has(early, "0 of"), "cut short with none killed: no \"0 of\"", early);
		b.Damage(kBandit2, kHero, 100, 0.0f, 2.5);
		b.Death(kBandit2, kHero, 2.5);
		const auto won = BuildMemory(b, {});
		Check(Has(won, "Both enemies were killed.") && !Has(won, "cut short"), "cut short with every enemy dead: told as won", won);
	}

	// A death-alternative mod puts the player back on their feet at a stroke: told as rising again, not as a rescue, and
	// never as a death; a heal that brought them back is not taken for it
	void TestRising()
	{
		Battle b;
		b.startedAt = 0;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 300));
		b.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
		b.Join(Info(kChief, "Bandit Chief", Side::kEnemy, 400));
		b.Damage(kHero, kChief, 300, 0.0f, 1);
		b.Down(kHero, 1);
		b.Sample(kHero, 0.0f, 1.2);
		b.Up(kHero);
		b.Sample(kHero, 1.0f, 1.5);  // Stay In The Fight: back up with full health, nobody healed
		b.Damage(kLydia, kChief, 290, 0.03f, 2);
		b.Heal(kLydia, kLydia, 200, "Potion of Plentiful Healing", 2.5);
		b.Sample(kLydia, 0.7f, 2.6);  // her own potion: not a rising
		b.Damage(kChief, kHero, 400, 0.0f, 3);
		b.Death(kChief, kHero, 3);
		b.endedAt = 4;
		const auto told = SummaryText(BuildSummary(b, kLydia, "Lydia", {}));
		Check(Has(told, "Kaira: ") && Has(told, "was struck down by Bandit Chief: collapsed and helpless for a moment, and rose again with their strength restored"),
			"back on their feet at a stroke: rose again", told);
		Check(Has(told, "Lydia was brought to the very edge of death by Bandit Chief, with almost no health left, but survived.") && !Has(told, "Lydia was brought to the very edge of death by Bandit Chief, with almost no health left, and rose"),
			"a potion that brought them back is no rising", told);
		const auto memory = BuildMemory(b, {});
		Check(Has(memory, "Kaira was struck down by Bandit Chief: collapsed and helpless for a moment, and rose again with their strength restored.") && !Has(memory, "survived."),
			"remembered once, without \"but survived\" on top", memory);
		Check(!Has(told, "saved Kaira") && !Has(memory, "saved Kaira"), "risen of their own: killing the one who struck them down saved nobody", told + memory);

		Battle late;  // the heal is reported a moment after the health came back
		late.startedAt = 0;
		late.Join(Info(kHero, "Kaira", Side::kPlayer, 300));
		late.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
		late.Join(Info(kChief, "Bandit Chief", Side::kEnemy, 400));
		late.Damage(kHero, kChief, 280, 0.05f, 1);
		late.Sample(kHero, 0.9f, 2);
		late.Heal(kHero, kLydia, 250, "Grand Healing", 2.4);
		late.Damage(kChief, kHero, 400, 0.0f, 3);
		late.Death(kChief, kHero, 3);
		late.endedAt = 4;
		const auto l = SummaryText(BuildSummary(late, kLydia, "Lydia", {}));
		Check(!Has(l, "rose again"), "a heal reported late still counts as the heal", l);
	}

	// A dungeon cleared room by room: one battle in several clashes, the time told over all of them
	void TestClashes()
	{
		Battle b;
		b.startedAt = 0;
		b.location = "Mzinchaleft";
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 300));
		b.Join(Info(kBandit1, "Dwarven Sphere", Side::kEnemy, 200));
		b.Damage(kBandit1, kHero, 200, 0.0f, 5);
		b.Death(kBandit1, kHero, 5);
		b.endedAt = 200;
		Check(Has(BuildMemory(b, {}), "A battle was fought at Mzinchaleft. It lasted about 3 minutes."), "one clash: as before", BuildMemory(b, {}));
		b.clashes = 3;
		const auto told = SummaryText(BuildSummary(b, kHero, "Kaira", {}));
		Check(Has(told, "A battle was fought at Mzinchaleft. It was fought in 3 clashes over about 3 minutes."), "three clashes, one line", told);
	}

	// The prompt's team guidance goes to those who fought on the player's side, not to enemies or witnesses
	void TestAlly()
	{
		Battle b;
		b.startedAt = 0;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 300));
		b.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
		b.Join(Info(kBandit1, "Bandit", Side::kEnemy, 100));
		b.Join(Info(kBandit2, "Bandit", Side::kEnemy, 100));
		b.Damage(kBandit1, kHero, 100, 0.0f, 1);
		b.Death(kBandit1, kHero, 1);
		b.Damage(kHero, kBandit2, 40, 0.8f, 2);
		b.Damage(kBandit2, kLydia, 50, 0.5f, 3);
		b.endedAt = 4;
		Check(BuildSummary(b, kLydia, "Lydia", {}).ally, "a follower is an ally", "");
		Check(!BuildSummary(b, kBandit2, "Bandit", {}).ally, "an enemy is not", "");
		Check(!BuildSummary(b, 0x999, "Hulda", {}).ally, "nor a witness", "");
	}

	// A bandit's power attack through another bandit: a real death, told apart, not as one of the party's kills
	void TestEnemyOwnSide()
	{
		Battle b;
		b.startedAt = 0;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 300));
		b.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
		b.Join(Info(kBandit1, "Ganeryk", Side::kEnemy, 100));
		b.Join(Info(kBandit2, "Reginus", Side::kEnemy, 100));
		b.Damage(kLydia, kBandit2, 30, 0.9f, 0.5);
		b.Damage(kBandit2, kBandit1, 100, 0.0f, 1);
		b.Death(kBandit2, kBandit1, 1);
		b.Damage(kBandit1, kLydia, 100, 0.0f, 2);
		b.Death(kBandit1, kLydia, 2);
		b.endedAt = 3;
		const auto told = SummaryText(BuildSummary(b, kHero, "Kaira", {}));
		Check(Has(told, "Who killed the enemies: Lydia 1 (Ganeryk).") && !Has(told, "Ganeryk 1"), "an enemy is not one of the killers", told);
		Check(Has(told, "Reginus was killed by Ganeryk, by accident: they were on the same side."), "the death is still told", told);
		Check(Has(told, "Both enemies were killed."), "and counted", told);

		Battle one;  // a single enemy who stopped fighting (Sever Actions made them peaceful)
		one.startedAt = 0;
		one.Join(Info(kHero, "Kaira", Side::kPlayer, 300));
		one.Join(Info(kBandit1, "Janekke", Side::kEnemy, 100));
		one.Damage(kBandit1, kHero, 20, 0.8f, 1);
		one.endedAt = 2;
		const auto o = BuildMemory(one, {});
		Check(Has(o, "The one enemy was not killed; the fight broke off.") && !Has(o, "None of the 1"), "one enemy, not killed", o);
	}

	// Whiterun, 1.6.0-beta-1: an ally's spell on a follower was told as their own side's attacks, and chickens only staggered
	// by a giant's stomp were listed as caught up in it although bystanders are left out
	void TestAllyNoise()
	{
		constexpr ActorId kChicken = 0xFF000E10, kGiant = 0xFF000E11;
		Battle b;
		b.startedAt = 0;
		b.omitBystanders = true;
		b.Join(Info(kHero, "Dave", Side::kPlayer, 300));
		b.Join(Info(kLydia, "Zhasim", Side::kPlayer, 300));
		b.Join(Info(kGiant, "Grunt", Side::kEnemy, 900));
		b.Join(Info(kGuard, "Durak", Side::kOther, 250));
		b.Join(Info(kChicken, "Chicken", Side::kOther, 5));
		b.Damage(kLydia, kGiant, 80, 0.7f, 1);
		b.Damage(kGiant, kGuard, 200, 0.7f, 1.5);
		b.Effect(kHero, kGuard, Text::Get("fx.armor_weakened"), "Frostbite", true, 2);
		b.Effect(kChicken, kGiant, Text::Get("fx.staggered"), "", true, 2.5);
		b.Damage(kGiant, kHero, 700, 0.0f, 3);
		b.Death(kGiant, kHero, 3);
		b.endedAt = 5;
		const auto dave = SummaryText(BuildSummary(b, kHero, "Dave", {}));
		Check(Has(dave, "Dave was caught in the crossfire, by accident") && !Has(dave, "own side's attacks"), "an ally's spell on one of the party: crossfire", dave);
		Check(!Has(dave, "Chicken") && !Has(dave, "caught up"), "a bystander only staggered is left out", dave);

		// Whiterun again: the town turned on the party. Fjori, who only fought the trolls and killed one, is an ally; Anoriath,
		// who struck the party, an enemy. Durak's aura on everyone is told to no one. A body cleaned up keeps its name.
		constexpr ActorId kTroll = 0xFF000E20, kFjori = 0xFF000E21, kAnoriath = 0xFF000E22, kDurak = 0xFF000E23;
		Battle w;
		w.startedAt = 0;
		w.omitBystanders = true;
		w.Join(Info(kHero, "Dave", Side::kPlayer, 300));
		w.Join(Info(kLydia, "Zhasim", Side::kPlayer, 300));
		auto troll = Info(kTroll, "Frost Troll", Side::kEnemy, 300);
		troll.wouldFight = true;
		w.Join(troll);
		w.Join(Info(kFjori, "Fjori Farroson", Side::kOther, 150));
		w.Join(Info(kAnoriath, "Anoriath", Side::kOther, 150));
		w.Join(Info(kDurak, "Durak", Side::kOther, 250));
		for (ActorId c = 0; c < 6; ++c) w.Join(Info(0xFF000E30 + c, "Chicken", Side::kOther, 5));
		w.Damage(kLydia, kTroll, 60, 0.8f, 1);
		w.Join(Info(kFjori, "Fjori Farroson", Side::kEnemy, 150));      // the town turns on the party
		w.Join(Info(kAnoriath, "Anoriath", Side::kEnemy, 150));
		for (const ActorId t : { kHero, kLydia, kFjori, kAnoriath, kTroll, ActorId(0xFF000E30), ActorId(0xFF000E31), ActorId(0xFF000E32), ActorId(0xFF000E33) })
			w.Effect(t, kDurak, Text::Get("fx.armor_weakened"), "", true, 1.5);
		w.Damage(kLydia, kAnoriath, 20, 0.85f, 2);
		w.Damage(kAnoriath, kLydia, 150, 0.0f, 2.5);
		w.Death(kAnoriath, kLydia, 2.5);
		w.Damage(kTroll, kFjori, 300, 0.0f, 3);
		w.Death(kTroll, kFjori, 3);
		auto gone = Info(kAnoriath, "someone", Side::kEnemy, 150);  // the body, cleaned up, reports nameless
		gone.unnamed = true;
		w.Join(gone);
		w.endedAt = 5;
		Check(w.EffectiveSide(*w.Find(kFjori)) == Side::kAlly && w.EffectiveSide(*w.Find(kAnoriath)) == Side::kEnemy,
			"turned hostile: an ally if they only fought the enemy, an enemy if they fought the party", "");
		const auto told = SummaryText(BuildSummary(w, kHero, "Dave", {}));
		Check(Has(told, "Dave and Zhasim fought against Frost Troll and Anoriath.") && Has(told, "Fjori Farroson also fought them, apart from the party.")
				&& Has(told, "Fjori Farroson 1 (Frost Troll)") && !Has(told, "by accident"),
			"the townsman who fought the troll is an ally, and their kill no accident", told);
		Check(!Has(told, "Durak") && !Has(told, "armor weakened") && !Has(told, "Chicken"), "an aura on everyone is told to no one", told);
		Check(!Has(told, "someone") && Has(told, "Zhasim 1 (Anoriath)"), "a body cleaned up keeps its name", told);

		// townsfolk the game put in combat (watching, about to flee), reached by nothing but the aura: not in it at all
		constexpr ActorId kOlava = 0xFF000E40;
		w.Join(Info(kOlava, "Olava the Feeble", Side::kOther, 60));
		w.CameToFight(kOlava);
		w.CameToFight(kDurak);
		w.Effect(kOlava, kDurak, Text::Get("fx.armor_weakened"), "", true, 1.6);
		const auto quiet = SummaryText(BuildSummary(w, kHero, "Dave", {}));
		Check(!Has(quiet, "Olava") && !Has(quiet, "Durak") && !Has(quiet, "caught up"), "in combat, but nothing done to them or by them: not told", quiet);
	}

	// abac: picked off from the shadows, the bandits never saw the player, and the one left searching learned their name from
	// the summary. Those who never saw one of the party are told "an unseen attacker"; the party knows who made the kills.
	void TestUnseen()
	{
		constexpr ActorId kSearcher = 0xFF000E50;
		Battle b;
		b.startedAt = 0;
		b.trackSight = true;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 300));
		b.Join(Info(kLydia, "Lydia", Side::kPlayer, 300));
		b.Join(Info(kBandit1, "Bandit", Side::kEnemy, 100));
		b.Join(Info(kBandit2, "Bandit", Side::kEnemy, 100));
		b.Join(Info(kSearcher, "Bandit Outlaw", Side::kEnemy, 100));
		b.Saw(kSearcher, kLydia);  // saw Lydia, never Kaira
		b.Damage(kBandit1, kHero, 100, 0.0f, 1);
		b.Death(kBandit1, kHero, 1);
		b.Damage(kBandit2, kHero, 100, 0.0f, 2);
		b.Death(kBandit2, kHero, 2);
		b.Damage(kSearcher, kLydia, 20, 0.8f, 3);
		b.CameToFight(kSearcher);
		b.endedAt = 10;

		const auto searcher = SummaryText(BuildSummary(b, kSearcher, "Bandit Outlaw", {}));
		Check(!Has(searcher, "Kaira") && Has(searcher, "An unseen attacker and Lydia fought against") && Has(searcher, "Who killed the enemies: an unseen attacker 2 (Bandit x2).") && !Has(searcher, "unseen)"),
			"one who never saw the player: an unseen attacker; Lydia, seen, by name", searcher);
		const auto witness = SummaryText(BuildSummary(b, 0xFF000E51, "Hulda", {}));
		Check(!Has(witness, "Kaira") && Has(witness, "an unseen attacker"), "a witness who never saw them either", witness);
		const auto lydia = SummaryText(BuildSummary(b, kLydia, "Lydia", {}));
		Check(Has(lydia, "Who killed the enemies: Kaira 2 (Bandit x2, 2 unseen).") && !Has(lydia, "unseen attacker"), "the party knows who, and that it was unseen", lydia);
		const auto hero = SummaryText(BuildSummary(b, kHero, "Kaira", {}));
		Check(Has(hero, "Kaira killed 2 of the 2 who died on the other side: Bandit x2.") && Has(hero, "2 of them without being seen."), "the killer's own account", hero);
		const auto memory = BuildMemory(b, {});
		Check(!Has(memory, "Kaira") && Has(memory, "Lydia"), "the remembered event, filed where the searcher lives: the player unnamed", memory);
		b.Saw(kSearcher, kHero);
		Check(Has(BuildMemory(b, {}), "Kaira and Lydia fought against"), "every enemy left alive saw them: named", BuildMemory(b, {}));
		const auto seenAll = SummaryText(BuildSummary(b, kSearcher, "Bandit Outlaw", {}));
		Check(!Has(seenAll, "Nobody saw") && !Has(seenAll, "unseen"), "one who saw them all along: nothing hidden, no unseen count", seenAll);

		// The Sleeping Giant: Sven killed unseen; the inn turned on the player, saw them, and Hjoromir was killed in
		// plain view. The inn knows who killed Hjoromir, not who killed Sven; the player's own account has both.
		constexpr ActorId kSven = 0xFF000E60, kHjoromir = 0xFF000E61, kOrgnar = 0xFF000E62;
		Battle inn;
		inn.startedAt = 0;
		inn.trackSight = true;
		inn.Join(Info(kHero, "Dave", Side::kPlayer, 300));
		inn.Join(Info(kSven, "Sven", Side::kEnemy, 100));
		inn.Damage(kSven, kHero, 100, 0.0f, 1);
		inn.Death(kSven, kHero, 1);
		inn.Join(Info(kHjoromir, "Hjoromir", Side::kEnemy, 100));
		inn.Saw(kOrgnar, kHero, 12);
		inn.Saw(kHjoromir, kHero, 12);
		inn.Damage(kHjoromir, kHero, 100, 0.0f, 14);
		inn.Death(kHjoromir, kHero, 14);
		inn.endedAt = 20;
		const auto orgnar = SummaryText(BuildSummary(inn, kOrgnar, "Orgnar", {}));
		Check(Has(orgnar, "Dave fought against") && Has(orgnar, "an unseen attacker 1 (Sven)") && Has(orgnar, "Dave 1 (Hjoromir)") && !Has(orgnar, "unseen)")
				&& Has(orgnar, "Nobody saw the unseen attacker"),
			"seen only after the first kill: that kill unseen, the next one named, and nobody's guess", orgnar);
		const auto dave = SummaryText(BuildSummary(inn, kHero, "Dave", {}));
		Check(Has(dave, "Dave 2 (Sven and Hjoromir, 1 unseen)") && !Has(dave, "Nobody saw"), "the player knows both, and which one was unseen", dave);
	}

	void TestDuration()
	{
		const auto is = [](double a_s, const char* a_want) { Check(DurationText(a_s) == a_want, a_want, DurationText(a_s)); };
		is(0.2, "1 second");
		is(42.4, "42 seconds");
		is(59.4, "59 seconds");
		is(59.8, "about 1 minute");
		is(60, "about 1 minute");
		is(170, "about 3 minutes");
		is(285, "about 5 minutes");
		is(300, "about 5 minutes");
		is(440, "about 5 minutes");
		is(460, "about 10 minutes");
		is(1560, "about 25 minutes");
		is(3600, "about 60 minutes");
		is(3700, "about 1 hour");
		is(5200, "about 1.5 hours");
		is(7300, "about 2 hours");
		const auto lydia = SummaryText(BuildSummary(Rescue(), kLydia, "Lydia", {}));
		Check(Has(lydia, "A battle was fought at Bleak Falls Barrow. It lasted 28 seconds."), "the battle's length is told", lydia);
	}

	void TestEdges()
	{
		Battle empty;
		Check(!BuildSummary(empty, kHero, "Kaira", {}).show, "no participants, nothing to show", "");
		Check(SummaryText(BuildSummary(empty, kHero, "Kaira", {})) == "(no battle to recall)", "empty summary text", "");

		Battle b;
		b.startedAt = 0;
		b.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		b.Join(Info(kBandit1, "Bandit", Side::kEnemy, 100));
		b.Damage(kHero, 0, 500, 0.0f, 1);  // a fall: no attacker
		b.Damage(kHero, kHero, 5, 0.0f, 1);  // self damage counts as no attacker
		b.Damage(kBandit1, 0x999, 10, 0.9f, 1);  // an attacker who never joined
		b.Damage(kBandit1, kHero, 1, 0.89f, 1);
		b.Heal(0x999, kHero, 10, "", 1);          // a target who never joined
		b.Death(0x999, kHero, 1);
		const auto ongoing = SummaryText(BuildSummary(b, kHero, "Kaira", {}));
		Check(Has(ongoing, "A battle is being fought right now.") && Has(ongoing, "So far 0 of the 1 enemies are dead."), "an ongoing battle", ongoing);
		Check(b.Find(kHero)->damageFrom.at(0) == 201.0f && b.Find(kHero)->kills.empty(), "unattributed damage, capped at the health it took, and unknown victims", ongoing);

		// overkill: a finishing blow counts for the health that was left
		Battle o;
		o.Join(Info(kHero, "Kaira", Side::kPlayer, 200));
		o.Join(Info(kBandit1, "Bandit", Side::kEnemy, 35));
		o.Join(Info(kBandit2, "Bandit", Side::kEnemy, 100));
		o.Damage(kBandit1, kHero, 1691.6f, 0.0f, 1);
		o.Damage(kBandit2, kHero, 60, 0.4f, 2);
		o.Damage(kBandit2, kHero, 500, 0.0f, 3);
		Check(o.Find(kBandit1)->damageTaken == 35.0f && o.Find(kBandit2)->damageTaken == 100.0f && o.Find(kHero)->damageDealt == 135.0f, "overkill is not counted", "");
		Check(!Significant(b, 3, 30.0) || b.Find(kHero)->WasCritical(), "significance", ongoing);
	}
}

int RunJourneys(const char* a_path);  // journeys.cpp

int main(int argc, char** argv)
{
	// build.ps1: the journey maps, every summary of a set of typical battles, for reading
	if (argc == 3 && std::string(argv[1]) == "--journeys") return RunJourneys(argv[2]);
	// build.ps1: write the reference file that ships with the mod
	if (argc == 3 && std::string(argv[1]) == "--write-reference") {
		std::FILE* f = std::fopen(argv[2], "wb");
		if (!f) return 2;
		const auto text = Text::Reference();
		std::fwrite(text.data(), 1, text.size(), f);
		std::fclose(f);
		return 0;
	}
	TestRescue();
	TestNearDeathAndSummon();
	TestOthers();
	TestBigFightAndBystanders();
	TestWildlifeAroundAFight();
	TestKillCreditFollowsTheFinalBlow();
	TestInjuriesAndDismemberment();
	TestInnocentsAndAllies();
	TestFoesSaveNobody();
	TestFriendlyFire();
	TestFriendlyFireOnBystanders();
	TestHarmlessEffects();
	TestKilledAtOnce();
	TestOwnWording();
	TestSayLessWhenInDoubt();
	TestNonCombatantsLeftOut();
	TestSummons();
	TestEnemySummonsDispatched();
	TestSameNames();
	TestDamageAndItems();
	TestRoles();
	TestEvenShares();
	TestCutShort();
	TestRising();
	TestClashes();
	TestAlly();
	TestEnemyOwnSide();
	TestAllyNoise();
	TestUnseen();
	TestDuration();
	TestEdges();
	std::printf("%d checks, %d failed\n", g_checks, g_failed);
	if (std::getenv("BSM_SHOW")) {
		const auto b = Rescue();
		std::printf("\n%s\n%s\n%s\n", SummaryText(BuildSummary(b, kLydia, "Lydia", {})).c_str(), SummaryText(BuildSummary(b, kHero, "Kaira", {})).c_str(),
			BuildMemory(b, {}).c_str());
	}
	return g_failed == 0 ? 0 : 1;
}
