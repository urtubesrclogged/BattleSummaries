// Journey maps: the same kinds of battle (a washout, a tough fight, a boss...) played out solo, as a duo, with a party of
// four followers and with all nine, and what each NPC is then told printed for reading. Balance is judged by reading,
// not checked: build.ps1 writes tests/journeys.txt with `BattleSummariesTests.exe --journeys <file>`, so a change to the
// rules shows as a diff of every summary.

#define _CRT_SECURE_NO_WARNINGS
#include "../src/Core.h"
#include "../src/Narrative.h"
#include "../src/Text.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace BSM;

namespace
{
	enum class Role { kPlayer, kTank, kMelee, kMage, kArcher, kSupport, kSummon };

	struct Member
	{
		ActorId     id;
		const char* name;
		Role        role;
		float       hp;
		int         master{ -1 };  // a summon or a raised corpse: the index of whoever commands it
	};

	// the player, then followers in the order they join: the first four are a party, all nine a large one
	const std::vector<Member> kCast{
		{ 0x14, "Kaira", Role::kPlayer, 300 },
		{ 0xFF000A01, "Lydia", Role::kTank, 350 },
		{ 0xFF000A02, "Marcurio", Role::kMage, 250 },
		{ 0xFF000A03, "Aela", Role::kArcher, 280 },
		{ 0xFF000A04, "Brelyna", Role::kSupport, 250 },
		{ 0xFF000A05, "J'zargo", Role::kMage, 250 },
		{ 0xFF000A06, "Uthgerd", Role::kTank, 330 },
		{ 0xFF000A07, "Jenassa", Role::kArcher, 260 },
		{ 0xFF000A08, "Erik", Role::kMelee, 280 },
		{ 0xFF000A09, "Borgakh", Role::kTank, 320 },
	};

	struct Foe
	{
		const char* name;
		float       hp;
	};

	using Weights = std::vector<float>;

	// a weight per role: how much of the party's damage each deals, or how much of the enemy's they take
	Weights ByRole(const std::vector<Member>& a_m, float a_player, float a_tank, float a_melee, float a_mage, float a_archer, float a_support,
		float a_summon = 0.8f)
	{
		Weights w;
		for (const auto& m : a_m) {
			const float by[] = { a_player, a_tank, a_melee, a_mage, a_archer, a_support, a_summon };
			w.push_back(by[static_cast<int>(m.role)]);
		}
		return w;
	}

	Weights Dealt(const std::vector<Member>& a_m) { return ByRole(a_m, 1.2f, 1.0f, 1.0f, 1.0f, 1.0f, 0.3f); }
	Weights Taken(const std::vector<Member>& a_m) { return ByRole(a_m, 1.2f, 1.6f, 1.2f, 0.7f, 0.5f, 0.6f, 4.0f); }
	Weights Even(const std::vector<Member>& a_m) { return Weights(a_m.size(), 1.0f); }

	// the member at a_index gets a_share of the whole; the others keep their weights between them
	Weights Lead(Weights a_w, std::size_t a_index, float a_share)
	{
		if (a_index >= a_w.size()) return a_w;
		float others = 0;
		for (std::size_t i = 0; i < a_w.size(); ++i)
			if (i != a_index) others += a_w[i];
		a_w[a_index] = others > 0 ? a_share / (1.0f - a_share) * others : 1.0f;
		return a_w;
	}

	struct Fight
	{
		Battle                   b;
		std::vector<Member>      party;
		std::vector<ActorId>     foes;
		std::vector<float>       health;  // the party's, as it goes
		std::vector<bool>        dead;
		double                   t{ 0 };
		// fought beside the party, not of it (Neloth, a town's guards): joined as the game sees them, neither side
		std::vector<std::pair<ActorId, std::string>> allies;

		void Ally(ActorId a_id, const char* a_name, float a_hp, ActorId a_master = 0, bool a_aggressive = false, bool a_lawful = false)
		{
			ActorInfo info{ a_id, a_name, Side::kOther, false, a_master, a_hp };
			info.summon = a_master != 0;
			info.wouldFight = a_aggressive;  // an atronach attacks anyone: read on its own, it would fight the player
			info.lawful = a_lawful;
			b.Join(info);
			if (!info.summon) allies.emplace_back(a_id, a_name);
		}

		void Join(const Member& a_m)
		{
			ActorInfo info{ a_m.id, a_m.name, Side::kPlayer, a_m.id == 0x14, a_m.master >= 0 ? party[a_m.master].id : 0, a_m.hp };
			info.summon = a_m.role == Role::kSummon;
			b.Join(info);
			party.push_back(a_m);
			health.push_back(a_m.hp);
			dead.push_back(false);
		}
		[[nodiscard]] int Find(Role a_role) const
		{
			for (std::size_t i = 0; i < party.size(); ++i)
				if (party[i].role == a_role && !dead[i]) return static_cast<int>(i);
			return -1;
		}
		[[nodiscard]] int Find(const char* a_name) const
		{
			for (std::size_t i = 0; i < party.size(); ++i)
				if (std::string(party[i].name) == a_name && !dead[i]) return static_cast<int>(i);
			return -1;
		}
		void Hurt(int a_i, ActorId a_by, float a_amount, float a_floor = 0.2f)
		{
			health[a_i] = std::max(health[a_i] - a_amount, party[a_i].hp * a_floor);
			b.Damage(party[a_i].id, a_by, a_amount, health[a_i] / party[a_i].hp, t);
			t += 0.4;
		}
		void Heal(int a_i, int a_by, float a_amount, const char* a_source)
		{
			if (a_i < 0 || a_by < 0) return;
			health[a_i] = std::min(health[a_i] + a_amount, party[a_i].hp);
			b.Heal(party[a_i].id, party[a_by].id, a_amount, a_source, t);
			t += 0.4;
		}
		void Kill(int a_i, ActorId a_by)
		{
			if (a_i < 0) return;
			b.Damage(party[a_i].id, a_by, health[a_i], 0.0f, t);
			b.Death(party[a_i].id, a_by, t);
			dead[a_i] = true;
			t += 0.4;
		}
		void Effect(ActorId a_target, int a_by, const char* a_key, const char* a_source, bool a_hostile)
		{
			if (a_by < 0) return;
			b.Effect(a_target, party[a_by].id, Text::Get(a_key), a_source, a_hostile, t);
			t += 0.4;
		}
		void Drink(int a_i, const char* a_potion, float a_heal)
		{
			if (a_i < 0) return;
			b.UsedItem(party[a_i].id, a_potion, false, t);
			Heal(a_i, a_i, a_heal, a_potion);
			t += 1.0;
		}
	};

	struct Journey
	{
		const char*                                          key;
		const char*                                          story;
		std::function<std::vector<Foe>(std::size_t)>         foes;   // by party size
		std::size_t                                          duo;    // the follower in a duo (index in kCast)
		float                                                taken;  // the enemy's damage, as a share of the party's total health
		std::function<Weights(const std::vector<Member>&)>   dealt;
		std::function<Weights(const std::vector<Member>&)>   hurt;
		std::function<void(Fight&, std::size_t)>             before; // before the party finishes off foe k
		bool                                                 solo{ true };
		std::function<void(Fight&)>                          setup{};  // before the first blow: summons cast
		int                                                  stopAfter{ -1 };  // the fight ends with this many foes down (before: k = stopAfter)
		bool                                                 cutShort{ false };  // ...because the player was moved away from it
		int                                                  clashes{ 1 };  // stopped and started again within the wind-down
	};

	std::vector<Foe> Many(const char* a_name, float a_hp, int a_count, std::vector<Foe> a_first = {})
	{
		for (int i = 0; i < a_count; ++i) a_first.push_back({ a_name, a_hp });
		return a_first;
	}

	void Play(Fight& a_f, const Journey& a_j)
	{
		auto& b = a_f.b;
		b.startedAt = 0;
		b.location = "the road to Whiterun";
		const auto members = std::move(a_f.party);
		a_f.party.clear();
		for (const auto& m : members) a_f.Join(m);
		if (a_j.setup) a_j.setup(a_f);
		const auto foes = a_j.foes(a_f.party.size());
		float foeHealth = 0, partyHealth = 0;
		for (std::size_t k = 0; k < foes.size(); ++k) {
			ActorInfo info{ 0xFF000B00 + static_cast<ActorId>(k), foes[k].name, Side::kEnemy, false, 0, foes[k].hp };
			info.wouldFight = true;
			b.Join(info);
			a_f.foes.push_back(info.id);
			foeHealth += foes[k].hp;
		}
		for (const auto& m : a_f.party) partyHealth += m.hp;
		std::vector<float> credit;  // kills follow the damage dealt: the largest remainder takes the next

		for (std::size_t k = 0; k < foes.size(); ++k) {
			if (static_cast<int>(k) == a_j.stopAfter) {
				if (a_j.before) a_j.before(a_f, k);
				break;
			}
			const ActorId foe = a_f.foes[k];
			// again for each foe: a corpse raised mid-fight joins in
			auto dealt = a_j.dealt(a_f.party);
			const auto hurt = a_j.hurt(a_f.party);
			credit.resize(a_f.party.size(), 0.0f);
			// the foe's share of the enemy's damage, among those still standing
			float hw = 0, dw = 0;
			for (std::size_t i = 0; i < a_f.party.size(); ++i) {
				if (a_f.dead[i]) continue;
				hw += hurt[i];
				dw += dealt[i];
			}
			const float share = foes[k].hp / foeHealth * a_j.taken * partyHealth;
			// every foe still standing is at it, not only the one about to fall
			const std::size_t standing = std::min<std::size_t>(foes.size() - k, a_j.stopAfter >= 0 ? foes.size() : 1);
			for (std::size_t i = 0; i < a_f.party.size() && hw > 0 && share > 0; ++i) {
				if (a_f.dead[i] || hurt[i] <= 0) continue;
				for (std::size_t by = 0; by < standing; ++by) a_f.Hurt(static_cast<int>(i), a_f.foes[k + by], share * hurt[i] / hw / standing);
			}
			if (a_j.before) a_j.before(a_f, k);
			if (const auto* v = b.Find(foe); v && v->dead) continue;  // someone else finished it (an ally)
			// before() may have raised a corpse into the party: every per-member list grows with it (credit read past its end
			// made the kills differ from run to run)
			dealt.resize(a_f.party.size(), 0.0f);
			credit.resize(a_f.party.size(), 0.0f);
			dw = 0;
			for (std::size_t i = 0; i < a_f.party.size(); ++i)
				if (!a_f.dead[i]) dw += dealt[i];

			int killer = -1;
			for (std::size_t i = 0; i < a_f.party.size(); ++i) {
				if (a_f.dead[i]) continue;
				credit[i] += dealt[i] / dw;
				if (killer < 0 || credit[i] > credit[killer]) killer = static_cast<int>(i);
			}
			credit[killer] -= 1.0f;
			float left = foes[k].hp;
			for (std::size_t i = 0; i < a_f.party.size(); ++i) {
				if (a_f.dead[i] || static_cast<int>(i) == killer || dealt[i] <= 0) continue;
				const float amount = foes[k].hp * dealt[i] / dw;
				left -= amount;
				b.Damage(foe, a_f.party[i].id, amount, left / foes[k].hp, a_f.t);
				a_f.t += 0.4;
			}
			b.Damage(foe, a_f.party[killer].id, left, 0.0f, a_f.t);
			b.Death(foe, a_f.party[killer].id, a_f.t);
			a_f.t += 1.0;
		}
		b.endedAt = a_f.t + 1.0;
		b.cutShort = a_j.cutShort;
		b.clashes = a_j.clashes;
	}

	std::vector<Journey> Journeys()
	{
		std::vector<Journey> j;
		j.push_back({ "Washout", "Three bandits go down without landing a blow.", [](std::size_t) { return Many("Bandit", 100, 3); }, 1, 0.0f, Dealt, Taken, {} });

		j.push_back({ "Tough",
			"A Briarheart and six Forsworn wear the party down: the player is injured, Lydia nearly falls, and someone dies (Lydia in a duo, "
			"Aela in a party). Brelyna heals when she is there; alone, the player drinks potions.",
			[](std::size_t) { return Many("Forsworn", 200, 6, { { "Forsworn Briarheart", 600 } }); }, 1, 0.65f, Dealt, Taken,
			[](Fight& f, std::size_t k) {
				const int player = 0, lydia = f.Find("Lydia"), healer = f.Find(Role::kSupport);
				if (k == 1) f.b.Injury(f.party[player].id, "a broken arm", false, f.t);
				if (k == 2 && lydia >= 0) {
					f.Hurt(lydia, f.foes[k], f.party[lydia].hp, 0.08f);
					f.Heal(lydia, healer, 150, "Healing Hands");
				}
				if (k == 3 && f.party.size() == 1) {
					f.Hurt(player, f.foes[k], f.party[player].hp, 0.1f);
					f.Drink(player, "Potion of Plentiful Healing", 150);
					f.Drink(player, "Potion of Plentiful Healing", 150);
				}
				if (k == 4) f.Kill(f.party.size() == 2 ? lydia : f.Find("Aela"), f.foes[k]);
				if (healer >= 0 && k % 2 == 1) f.Heal(player, healer, 60, "Heal Other");
			} });

		j.push_back({ "Lopsided: player", "The player deals most of the damage (85%) and most of the kills.", [](std::size_t) { return Many("Bandit", 150, 7); }, 1,
			0.2f, [](const std::vector<Member>& m) { return Lead(Dealt(m), 0, 0.85f); }, Taken, {} });

		j.push_back({ "Lopsided: follower", "Marcurio deals most of the damage (80%) and most of the kills.", [](std::size_t) { return Many("Draugr", 200, 7); }, 2,
			0.2f, [](const std::vector<Member>& m) {
				std::size_t star = 0;
				while (star < m.size() && std::string(m[star].name) != "Marcurio") ++star;
				return Lead(Dealt(m), star, 0.8f);
			}, Taken, {}, false });

		j.push_back({ "Mobs", "Twenty skeevers, thirty health each.", [](std::size_t) { return Many("Skeever", 30, 20); }, 1, 0.15f, Dealt, Taken, {} });

		j.push_back({ "Boss",
			"One enemy soaks up the party's damage: a giant alone, a dragon otherwise. Lydia takes 60% of what it deals; the player drinks a "
			"potion, Brelyna heals Lydia.",
			[](std::size_t n) { return n == 1 ? std::vector<Foe>{ { "Giant", 1500 } } : std::vector<Foe>{ { "Blood Dragon", 3000 } }; }, 1, 0.4f, Dealt,
			[](const std::vector<Member>& m) { return Lead(Even(m), m.size() > 1 ? 1 : 0, 0.6f); },
			[](Fight& f, std::size_t) {
				f.Drink(0, "Potion of Healing", 50);
				f.Heal(f.Find("Lydia"), f.Find(Role::kSupport), 80, "Heal Other");
			} });

		j.push_back({ "Balanced", "Eight bandits; damage dealt, kills and blows taken shared evenly.", [](std::size_t) { return Many("Bandit", 150, 8); }, 1, 0.3f,
			Even, Even, {} });

		j.push_back({ "Teamwork",
			"A bandit chief and five bandits. The player holds the front (55% of the blows), Marcurio burns and paralyzes and frightens, Aela "
			"shoots and poisons, Brelyna heals and casts Oakflesh. The duo is the player and Brelyna.",
			[](std::size_t) { return Many("Bandit", 150, 5, { { "Bandit Chief", 400 } }); }, 4, 0.35f,
			[](const std::vector<Member>& m) { return ByRole(m, 1.0f, 0.8f, 0.8f, 1.2f, 1.0f, 0.2f); },
			[](const std::vector<Member>& m) { return Lead(ByRole(m, 1.0f, 1.0f, 1.0f, 0.4f, 0.2f, 0.4f), 0, 0.55f); },
			[](Fight& f, std::size_t k) {
				const int mage = f.Find(Role::kMage), archer = f.Find(Role::kArcher), support = f.Find(Role::kSupport);
				if (k == 0) {
					f.Effect(f.party[0].id, support, "fx.armored", "Oakflesh", false);
					const int tank = f.Find(Role::kTank);
					f.Effect(f.party[tank >= 0 ? tank : 0].id, support, "fx.armored", "Oakflesh", false);
					if (mage >= 0) f.Effect(f.party[mage].id, support, "fx.armored", "Oakflesh", false);
					if (archer >= 0) {
						f.b.UsedItem(f.party[archer].id, "Poison of Lingering Damage", true, f.t);
						f.t += 1.0;
					}
				}
				if (k == 1) f.Effect(f.foes[k], mage, "fx.paralyzed", "Paralyze", true);
				if (k == 2) f.Effect(f.foes[k], mage, "fx.fear", "Fear", true);
				if (k == 1 || k == 3) f.Heal(0, support, 60, "Heal Other");
			} });
		j.push_back({ "Summoners",
			"Six bandits. The player sends in a flame atronach, Brelyna another when she is there, and the player raises the first bandit to "
			"fall. The summons draw most of the enemy's blows. The duo is the player and Brelyna.",
			[](std::size_t) { return Many("Bandit", 150, 6); }, 4, 0.4f, Dealt, Taken,
			[](Fight& f, std::size_t k) {
				if (k == 1) f.Join({ 0xFF000D00, "Bandit", Role::kSummon, 150, 0 });
			},
			true,
			[](Fight& f) {
				f.Join({ 0xFF000D01, "Flame Atronach", Role::kSummon, 300, 0 });
				if (const int brelyna = f.Find("Brelyna"); brelyna >= 0) f.Join({ 0xFF000D02, "Flame Atronach", Role::kSummon, 300, brelyna });
			} });
		j.push_back({ "Carried off",
			"Six Forsworn. Two fall, then the player is struck down and carried away mid-fight (a death-alternative mod like Revenge and "
			"Respawn, or a teleport): the battle ends there with four Forsworn alive.",
			[](std::size_t) { return Many("Forsworn", 200, 6); }, 1, 0.5f, Dealt, Taken,
			[](Fight& f, std::size_t k) {
				if (k == 2) {
					f.Hurt(0, f.foes[k], f.party[0].hp, 0.0f);
					f.b.Down(f.party[0].id, f.t);
				}
			} });
		j.back().stopAfter = 2;
		j.back().cutShort = true;

		j.push_back({ "Back on your feet",
			"Six bandits. The player is struck down twice, and each time a death-alternative mod (Stay In The Fight, Sacrosanct's "
			"Fortitude) puts them straight back on their feet with their health restored. Nobody heals them.",
			[](std::size_t) { return Many("Bandit", 150, 6); }, 1, 0.3f, Dealt, Taken,
			[](Fight& f, std::size_t k) {
				if (k != 1 && k != 4) return;
				f.Hurt(0, f.foes[k], f.party[0].hp, 0.0f);
				f.b.Down(f.party[0].id, f.t);
				f.b.Up(f.party[0].id);
				f.t += 0.25;
				f.health[0] = f.party[0].hp;
				f.b.Sample(f.party[0].id, 1.0f, f.t);
				f.t += 0.4;
			} });

		j.push_back({ "Rescued",
			"Six bandits. The player is struck down; Lydia kills the bandit standing over them and Brelyna heals them when she is "
			"there. The player gets up slowly, as a follower does.",
			[](std::size_t) { return Many("Bandit", 150, 6); }, 1, 0.3f, Dealt, Taken,
			[](Fight& f, std::size_t k) {
				if (k != 2) return;
				f.Hurt(0, f.foes[k], f.party[0].hp, 0.0f);
				f.b.Down(f.party[0].id, f.t);
				f.Heal(0, f.Find(Role::kSupport), 120, "Healing Hands");
				f.b.Up(f.party[0].id);
			} });

		j.push_back({ "Dungeon crawl",
			"A Dwemer ruin cleared room by room: six Dwarven Spheres and Spiders met two at a time, each group within the wind-down "
			"of the last. One battle, fought in three clashes.",
			[](std::size_t) { return Many("Dwarven Spider", 120, 4, { { "Dwarven Sphere", 300 }, { "Dwarven Sphere", 300 } }); }, 1, 0.25f, Dealt, Taken,
			[](Fight& f, std::size_t k) {
				if (k == 2 || k == 4) f.t += 60.0;  // on to the next room
			} });
		j.back().clashes = 3;

		j.push_back({ "Fled",
			"Six Forsworn are too much: two fall, the party takes a beating and runs, and the Forsworn give up the chase. The battle ends "
			"where it was, with four Forsworn alive.",
			[](std::size_t) { return Many("Forsworn", 200, 6); }, 1, 0.8f, Dealt, Taken,
			[](Fight& f, std::size_t k) {
				if (k == 2) f.Drink(0, "Potion of Healing", 50);
			} });
		j.back().stopAfter = 2;

		// Neloth at Nchardak (a player's log, 1.5.0): not a follower, fighting the same automatons with his own Ash Guardian.
		// His spells catch a follower (the player, alone); his guardian takes blows and finishes a sphere. Told as enemies
		// before: Neloth "fled", and his guardian's kill was an enemy's "by accident" of their own side.
		j.push_back({ "Allies: Neloth",
			"Five Dwemer automatons. Neloth (not a follower) fights them too, with his Ash Guardian: he strikes every one, his "
			"spells catch a follower, the guardian draws blows and finishes the second Dwarven Sphere.",
			[](std::size_t) { return Many("Dwarven Spider", 150, 3, { { "Dwarven Sphere", 300 }, { "Dwarven Sphere", 300 } }); }, 1, 0.3f, Dealt, Taken,
			[](Fight& f, std::size_t k) {
				const ActorId neloth = 0xFF000E00, guardian = 0xFF000E01, foe = f.foes[k];
				f.b.Damage(foe, neloth, 60, -1.0f, f.t);
				f.t += 0.4;
				f.b.Damage(guardian, foe, 90, -1.0f, f.t);
				f.t += 0.4;
				if (k == 1) {
					const int hit = f.party.size() > 1 ? 1 : 0;
					f.Hurt(hit, neloth, 70);
				}
				if (k == 1) {
					f.b.Damage(foe, guardian, 240, 0.0f, f.t);
					f.b.Death(foe, guardian, f.t);
					f.t += 1.0;
				}
			},
			true,
			[](Fight& f) {
				f.Ally(0xFF000E00, "Neloth", 400);
				f.Ally(0xFF000E01, "Ash Guardian", 600, 0xFF000E00, true);
			} });

		// abac's report: a sneak archer's victims, and the bandit left searching, learned the player's name from the summary
		j.push_back({ "Stealth",
			"Four bandits. The player picks off three from the shadows and is never seen; the fourth searches and gives up. Lydia, "
			"when she is there, is seen by every bandit.",
			[](std::size_t) { return Many("Bandit", 120, 4); }, 1, 0.05f, [](const std::vector<Member>& m) { return Lead(Dealt(m), 0, 0.9f); }, Taken,
			{}, true,
			[](Fight& f) {
				f.b.trackSight = true;
				if (const int lydia = f.Find("Lydia"); lydia >= 0)
					for (ActorId k = 0; k < 4; ++k) f.b.Saw(0xFF000B00 + k, f.party[lydia].id);
			} });
		j.back().stopAfter = 3;

		// a dragon at Whiterun: six guards fight it on their own account and one of them dies; the party lands the killing blow
		j.push_back({ "Allies: guards and a dragon",
			"A Blood Dragon at Whiterun. Six Whiterun guards fight it too: their arrows do a good part of the damage, and the dragon "
			"kills one of them. The party kills it.",
			[](std::size_t) { return std::vector<Foe>{ { "Blood Dragon", 3000 } }; }, 1, 0.3f, Dealt, Taken,
			[](Fight& f, std::size_t k) {
				for (ActorId g = 0; g < 6; ++g) {
					f.b.Damage(f.foes[k], 0xFF000F00 + g, 120, -1.0f, f.t);
					f.t += 0.3;
				}
				f.b.Damage(0xFF000F02, f.foes[k], 220, 0.0f, f.t);
				f.b.Death(0xFF000F02, f.foes[k], f.t);
				f.t += 0.5;
			},
			true,
			[](Fight& f) {
				for (ActorId g = 0; g < 6; ++g) f.Ally(0xFF000F00 + g, "Whiterun Guard", 220, 0, false, true);
			} });
		return j;
	}

	void Print(std::FILE* a_out, const char* a_head, const std::vector<std::string>& a_lines)
	{
		if (a_lines.empty()) return;
		std::fprintf(a_out, "  [%s]\n", a_head);
		for (const auto& l : a_lines) std::fprintf(a_out, "    %s\n", l.c_str());
	}
}

// every journey, every variant; 0 when it ran
int RunJourneys(const char* a_path)
{
	std::FILE* out = std::fopen(a_path, "wb");
	if (!out) return 2;
	std::fprintf(out,
		"Battle Summaries journey maps: what each NPC is told after the same kinds of battle, solo, as a duo, with a party of four\n"
		"followers and with nine. Written by build.ps1; read the diff when a rule changes. The player (Kaira) is told nothing:\n"
		"summaries go to the NPCs. Solo, the one shown is a witness who watched.\n");
	const struct { const char* name; std::size_t size; } variants[] = { { "solo", 1 }, { "duo", 2 }, { "party", 5 }, { "large", 10 } };
	int maps = 0;
	for (const auto& j : Journeys()) {
		for (const auto& v : variants) {
			if (v.size == 1 && !j.solo) continue;
			Fight f;
			if (v.size == 2)
				f.party = { kCast[0], kCast[j.duo] };
			else
				f.party.assign(kCast.begin(), kCast.begin() + v.size);
			Play(f, j);
			++maps;
			std::fprintf(out, "\n==== %s / %s (%zu) ====\n%s\n", j.key, v.name, v.size, j.story);
			std::fprintf(out, "  [remembered event]\n    %s\n", BuildMemory(f.b, {}).c_str());
			if (v.size == 1) {
				const auto s = BuildSummary(f.b, 0xFF000C00, "a witness", {});
				Print(out, "told to everyone", s.general);
				Print(out, "a witness, about the fighters", s.others);
			} else {
				bool first = true;
				for (std::size_t i = 1; i < f.party.size(); ++i) {
					if (f.party[i].role == Role::kSummon) continue;
					const auto s = BuildSummary(f.b, f.party[i].id, f.party[i].name, {});
					if (first) Print(out, "told to everyone", s.general);
					Print(out, (std::string(f.party[i].name) + ", about themself").c_str(), s.personal);
					if (first) Print(out, (std::string(f.party[i].name) + ", about the others").c_str(), s.others);
					first = false;
				}
			}
			// the first enemy left alive, from their side of it: what one who lived through it was told
			for (const auto foe : f.foes) {
				const auto* v = f.b.Find(foe);
				if (!v || v->dead) continue;
				const auto s = BuildSummary(f.b, foe, v->info.name, {});
				Print(out, (v->info.name + " (an enemy who lived), told").c_str(), s.general);
				Print(out, (v->info.name + " (an enemy who lived), about the party").c_str(), s.others);
				break;
			}
			// an ally, from their own side of it: the first of each name
			std::vector<std::string> shown;
			for (const auto& [id, name] : f.allies) {
				if (std::ranges::find(shown, name) != shown.end()) continue;
				shown.push_back(name);
				const auto s = BuildSummary(f.b, id, name, {});
				Print(out, (name + " (an ally), about themself").c_str(), s.personal);
				Print(out, (name + " (an ally), about the party").c_str(), s.others);
			}
		}
	}
	std::fclose(out);
	std::printf("%d journey maps written to %s\n", maps, a_path);
	return 0;
}
