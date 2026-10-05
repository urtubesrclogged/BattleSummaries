// Offline tests of the battle model and its narrative: no game, no SkyrimNet. Run by build.ps1 before the DLL is
// accepted. Each scenario replays a fight as the tracker would report it and checks what an NPC is then told.

#define _CRT_SECURE_NO_WARNINGS
#include "../src/Core.h"
#include "../src/Narrative.h"

#include <cstdio>
#include <cstdlib>
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
		Check(Has(lydia, "Kaira: took only light wounds; was never near death; killed 3 of the other side."), "the player's line among the others", lydia);
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
		Check(Has(hero, "Kaira was brought to the very edge of death by Wolf"), "near death without bleedout", hero);
		Check(Has(hero, "Wolf, who had nearly killed Kaira, was then killed by Flame Atronach (commanded by Kaira)."), "the summon's kill, with its master", hero);
		Check(!Has(hero, "saved Kaira's life"), "your own summon's kill is not someone else's rescue", hero);
		Check(Has(hero, "Kaira healed themself with Potion of Healing"), "a potion is a self heal", hero);
		Check(Has(hero, "1 of the 2 enemies were killed; the rest fled, yielded or broke off."), "partial outcome", hero);
		Check(Has(hero, "Flame Atronach (commanded by Kaira) 1 (Wolf)"), "kill tally credits the summon with its master", hero);
	}

	// A guard who only fought the bandits counts with the player's side; a townsman who hit the player counts against.
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
		Check(b.EffectiveSide(*b.Find(kGuard)) == Side::kPlayer, "the guard fought on the player's side", "");
		Check(b.EffectiveSide(*b.Find(kMage)) == Side::kEnemy, "the farmer fought against the player", "");
		const auto hero = SummaryText(BuildSummary(b, kHero, "Kaira", {}));
		Check(Has(hero, "Kaira and Whiterun Guard fought against Bandit and Angry Farmer."), "sides with the others folded in", hero);
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
		Check(Has(text, "Kaira and Jenassa fought against Minotaur.") && Has(text, "The one enemy was killed.") && Has(text, "Who killed the enemies: Kaira 1 (Minotaur)."),
			"the battle is the party against the minotaur", text);
		Check(!Has(text, "Echatere") && !Has(text, "Crab") && !Has(text, "Elk") && !Has(text, "Horse") && !Has(text, "caught up"), "the wildlife's own quarrels are not told", text);
		Check(Has(text, "Kaira went into this fight already close to death from earlier wounds") && !Has(text, "edge of death"), "already wounded going in", text);
		const auto other = SummaryText(BuildSummary(b, kLydia, "Jenassa", {}));
		Check(Has(other, "Kaira: took no damage worth the name; went in already close to death from earlier wounds; killed 1 of the other side."), "the companion's line", other);

		// someone who fights an enemy of the party is part of it; hurt further while already low counts as near death
		Battle c;
		c.Join(hero);
		c.Join(Info(kMinotaur, "Minotaur", Side::kEnemy, 300));
		c.Join(Info(kGuard, "Whiterun Guard", Side::kOther, 200));
		c.Damage(kHero, kMinotaur, 8, 0.04f, 1);
		c.Damage(kMinotaur, kGuard, 50, 0.8f, 2);
		c.endedAt = 10;
		Check(c.Involved(*c.Find(kGuard)) && c.EffectiveSide(*c.Find(kGuard)) == Side::kPlayer, "a guard who fights the party's enemy is on the party's side", "");
		Check(c.Find(kHero)->WasCritical() && c.Find(kHero)->broughtLowBy == kMinotaur, "hurt further while already low is near death", "");
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

int main()
{
	TestRescue();
	TestNearDeathAndSummon();
	TestOthers();
	TestBigFightAndBystanders();
	TestWildlifeAroundAFight();
	TestEdges();
	std::printf("%d checks, %d failed\n", g_checks, g_failed);
	if (std::getenv("BSM_SHOW")) {
		const auto b = Rescue();
		std::printf("\n%s\n%s\n%s\n", SummaryText(BuildSummary(b, kLydia, "Lydia", {})).c_str(), SummaryText(BuildSummary(b, kHero, "Kaira", {})).c_str(),
			BuildMemory(b, {}).c_str());
	}
	return g_failed == 0 ? 0 : 1;
}
