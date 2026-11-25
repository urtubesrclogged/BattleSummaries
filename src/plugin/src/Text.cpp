#include "Text.h"

#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace BSM::Text
{
	namespace
	{
		// The built-in wording. Order is the order of the reference file.
		const std::vector<Entry> kDefaults{
			// ---------------------------------------------------------------- the battle as a whole
			{ "Battle", "battle.underway", "A battle is being fought{at_place} right now.", "{at_place} is empty or a space and battle.at" },
			{ "Battle", "battle.fought", "A battle was fought{at_place}. It lasted {duration}.", "" },
			{ "Battle", "battle.allies", "{allies} also fought them, apart from the party.",
				"someone not of the party who fought the party's enemies (Neloth at Nchardak, Whiterun's guards at a dragon)" },
			{ "Battle", "battle.fought_clashes", "A battle was fought{at_place}. It was fought in {count} clashes over {duration}.",
				"fighting that stopped and started again within the wind-down: a dungeon cleared room by room, enemies arriving in groups" },
			{ "Battle", "battle.at", "at {place}", "" },
			{ "Battle", "battle.wilds", "the wilds of {place}", "open country: {place} is the worldspace, \"Skyrim\"" },
			{ "Battle", "battle.sides", "{party} fought against {enemies}.", "" },
			{ "Battle", "battle.enemies_only", "The enemies were {enemies}.", "" },
			{ "Battle", "battle.caught_up", "Also caught up in it: {others}.", "those who were struck but did not fight" },

			{ "Duration", "duration.second", "1 second", "" },
			{ "Duration", "duration.seconds", "{count} seconds", "" },
			{ "Duration", "duration.minute", "about 1 minute", "" },
			{ "Duration", "duration.minutes", "about {count} minutes", "" },
			{ "Duration", "duration.hour", "about 1 hour", "" },
			{ "Duration", "duration.hours", "about {count} hours", "" },
			{ "Duration", "duration.hours_half", "about {count}.5 hours", "" },

			{ "Age", "age.now", "now", "how long ago the battle was; the prompt puts it in its heading" },
			{ "Age", "age.moments", "moments ago", "" },
			{ "Age", "age.short", "a short while ago", "" },
			{ "Age", "age.hour_or_two", "an hour or two ago", "" },
			{ "Age", "age.hours", "some hours ago", "" },

			// ---------------------------------------------------------------- names and lists
			{ "Lists", "list.and", "and", "between the last two of a list: \"a, b and c\"" },
			{ "Lists", "list.times", "{name} x{count}", "several of the same name" },
			{ "Lists", "list.others", "{count} others", "the rest of a list too long to tell" },
			{ "Lists", "name.someone", "someone", "an actor with no name" },
			{ "Lists", "name.unseen", "an unseen attacker", "one of the player's side the reader never saw (a sneak attack): their name is not known" },
			{ "Lists", "name.unseen_count", "{count} unseen attackers", "several of them, in a list" },
			{ "Lists", "name.nobody", "no one in particular", "damage or a kill with nobody behind it" },
			{ "Lists", "name.commanded", "{name} (commanded by {master})", "a summoned or raised creature" },
			{ "Lists", "name.summons_of", "{master}'s {summons}", "a summoned or raised creature, by whoever commands it: it drew the enemy's blows" },
			{ "Lists", "name.summons_many", "{master}'s summons", "the same, for several of them" },
			{ "Lists", "name.nth", "the {ordinal} {name}", "one of several fighters of the same name, by the order they joined" },
			{ "Lists", "name.numbered", "{name} number {count}", "the same, past the tenth" },
			{ "Lists", "ordinal.1", "first", "" },
			{ "Lists", "ordinal.2", "second", "" },
			{ "Lists", "ordinal.3", "third", "" },
			{ "Lists", "ordinal.4", "fourth", "" },
			{ "Lists", "ordinal.5", "fifth", "" },
			{ "Lists", "ordinal.6", "sixth", "" },
			{ "Lists", "ordinal.7", "seventh", "" },
			{ "Lists", "ordinal.8", "eighth", "" },
			{ "Lists", "ordinal.9", "ninth", "" },
			{ "Lists", "ordinal.10", "tenth", "" },

			// ---------------------------------------------------------------- how it ended
			{ "Outcome", "outcome.word_enemies", "enemies", "" },
			{ "Outcome", "outcome.word_opponents", "opponents", "" },
			{ "Outcome", "outcome.so_far", "So far {dead} of the {total} {foes} are dead.", "" },
			{ "Outcome", "outcome.one", "The one enemy was killed.", "" },
			{ "Outcome", "outcome.all", "All {total} {foes} were killed.", "" },
			{ "Outcome", "outcome.both", "Both {foes} were killed.", "the same, with two of them" },
			{ "Outcome", "outcome.none", "None of the {total} {foes} were killed; the fight broke off.", "" },
			{ "Outcome", "outcome.none_one", "The one enemy was not killed; the fight broke off.", "the same, with one enemy" },
			{ "Outcome", "outcome.some", "{dead} of the {total} {foes} were killed; the rest fled, yielded or broke off.", "" },
			{ "Outcome", "outcome.cut_short", "The fight was cut short; by then {dead} of the {total} {foes} had been killed.",
				"the player was moved away while enemies were still alive (a teleport, a door, a death-alternative mod)" },
			{ "Outcome", "outcome.cut_short_none", "The fight was cut short before any of the {total} {foes} fell.", "the same, with none of them killed" },
			{ "Outcome", "outcome.cut_short_one", "The fight was cut short before the one enemy fell.", "the same, against one enemy" },
			{ "Outcome", "tally.line", "Who killed the enemies: {tally}.", "{tally} reads \"Kaira 3 (Bandit x2 and Bandit Chief); Lydia 1 (Bandit)\"" },
			{ "Outcome", "tally.unknown", "unknown causes", "" },
			{ "Outcome", "battle.damage_by", "Most of the party's damage to the enemy was dealt by {names}.", "a kill goes to the last blow; this says who did the work: those who stood out, when they did over half of it" },
			{ "Outcome", "battle.damage_led", "{names} led the party's damage to the enemy.", "those who stood out, when they did less than half of it between them" },
			{ "Outcome", "battle.damage_even", "The party shared the damage to the enemy evenly.", "nobody stood out" },
			{ "Outcome", "battle.damage_also", "{names} also did a real part of it.", "others whose part of the party's damage was at least a fifth (an even share, in a party of more than five)" },
			{ "Roles", "role.front_none", "The enemy never landed a blow on the party.", "the party took no damage from the enemy at all" },
			{ "Roles", "role.front_brunt", "{names} bore the brunt of the enemy's blows.", "took over half of all the party took from the enemy (three parts in four, in a duo)" },
			{ "Roles", "role.front_also", "{names} also took a share of the enemy's blows.", "the others who took at least a fifth of it (an even share, in a party of more than five)" },
			{ "Roles", "role.front_shared", "{names} took the enemy's blows between them.", "no one took over half; these took at least a fifth each (an even share, in a party of more than five)" },
			{ "Roles", "role.front_even", "The enemy's blows fell evenly across the party.", "nobody stood out" },
			{ "Roles", "role.front_most", "{names} took the largest share of the enemy's blows.", "" },
			{ "Roles", "role.healer", "{names} kept the others on their feet with healing.", "at least a fifth of the healing the party gave one another" },
			{ "Roles", "role.healer_one", "{names} kept {other} going with healing.", "the same, when the healing went to one other person" },
			{ "Roles", "role.protector", "{names} shielded the others with magic.", "wards, magical armor, resistances cast on the others" },
			{ "Roles", "role.protector_one", "{names} shielded {other} with magic.", "the same, cast on one other person" },
			{ "Roles", "role.controller", "{names} held the enemy back ({effects}).", "paralysis, fear, slows and the like that took hold on the enemy" },
			{ "Outcome", "share.percent", "(about {percent}%)", "only with bShowNumbers: a fighter's part of the party's damage" },
			{ "Outcome", "summons.dispatched", "Summoned creatures dispatched: {tally}.", "the enemy's summoned or raised creatures, by who put an end to them" },
			{ "Outcome", "tally.through", "{count} through {summons}", "kills a summoned creature made, credited to whoever summoned it" },
			{ "Outcome", "tally.unseen", "{count} unseen", "kills of victims who never saw their killer; only to those who know who the killer was" },

			// ---------------------------------------------------------------- deaths
			{ "Deaths", "killed.by", "was killed by {killer}", "" },
			{ "Deaths", "killed.unknown", "was killed, by whom is not known", "" },
			{ "Deaths", "killed.line", "{name} {killed}{own_side}.", "{killed} is killed.by or killed.unknown; {own_side} is empty or a comma and killed.own_side" },
			{ "Deaths", "killed.own_side", "by accident: they were on the same side", "an ally killed by their own side" },
			{ "Deaths", "killed.not_fighting", "{name}, who was not actively engaged in combat, {killed}.",
				"a citizen or guard killed without having raised a weapon or come at anyone" },

			// ---------------------------------------------------------------- wounds taken, damage done
			{ "Wounds", "personal.wounds_dealt", "{name} {wounds} and {dealt}.", "{wounds} is one of wounds.*, {dealt} one of dealt.*" },
			{ "Wounds", "wounds.none", "took no damage worth the name", "" },
			{ "Wounds", "wounds.light", "took only light wounds", "" },
			{ "Wounds", "wounds.solid", "took solid wounds", "" },
			{ "Wounds", "wounds.bad", "was badly hurt, taking nearly a full body's worth of damage", "" },
			{ "Wounds", "wounds.beyond_healed", "took more damage than a body can survive in one go, and only kept going thanks to healing", "" },
			{ "Wounds", "wounds.brutal", "took a brutal amount of damage", "" },
			{ "Wounds", "dealt.none", "dealt no damage to anyone", "" },
			{ "Wounds", "dealt.all", "dealt all the damage on their side", "" },
			{ "Wounds", "dealt.small", "dealt only a small part of their side's damage", "" },
			{ "Wounds", "dealt.fair", "dealt a fair share of their side's damage", "" },
			{ "Wounds", "dealt.large", "dealt a large share of their side's damage", "" },
			{ "Wounds", "dealt.most", "dealt most of their side's damage", "" },
			{ "Wounds", "points", "(about {points} points)", "only with bShowNumbers" },

			// ---------------------------------------------------------------- close to death
			{ "NearDeath", "personal.fact", "{name} {fact}.", "{fact} is brink.down or brink.near_death" },
			{ "NearDeath", "brink.down", "was struck down{times}{by}: collapsed and helpless for a moment",
				"{times} is empty or a space and times.*; {by} is empty or a space and brink.by" },
			{ "NearDeath", "brink.near_death", "was brought to the very edge of death{by}, with almost no health left", "" },
			{ "NearDeath", "brink.got_up", ", but got back up", "after brink.down: how they came back from it" },
			{ "NearDeath", "brink.survived", ", but survived", "after brink.near_death" },
			{ "NearDeath", "brink.rose", ", and rose again with their strength restored",
				"after either, in place of the two above: back on their feet at a stroke with nobody's healing (a death-alternative mod, a power)" },
			{ "NearDeath", "brink.by", "by {name}", "" },
			{ "NearDeath", "brink.friendly", "(by accident: friendly fire)", "added after brink.by when it was their own side" },
			{ "NearDeath", "times.twice", "twice", "" },
			{ "NearDeath", "times.many", "{count} times", "" },
			{ "NearDeath", "personal.already_low", "{name} went into this fight already close to death from earlier wounds, and came through it without being hurt much further.", "" },
			{ "NearDeath", "personal.hurt_badly", "{name} was hurt badly at the worst of it, but was never at death's door.", "" },
			{ "NearDeath", "personal.no_danger", "{name} was never in serious danger.", "" },

			// ---------------------------------------------------------------- who saved whom
			{ "Rescue", "rescue.struck_down", "struck down", "what the assailant had done: fills {what}" },
			{ "Rescue", "rescue.nearly_killed", "nearly killed", "" },
			{ "Rescue", "rescue.recovered", "{name} recovered enough to kill {assailant}, who had {what} {name}.", "" },
			{ "Rescue", "rescue.assailant_killed", "{assailant}, who had {what} {name}, was then killed by {killer}.", "" },
			{ "Rescue", "rescue.recovered_wounded", "{name} recovered enough to kill {assailant}, who had also wounded {name} badly.", "one who did much of the damage but not the blow that did it" },
			{ "Rescue", "rescue.assailant_wounded", "{assailant}, who had also wounded {name} badly, was then killed by {killer}.", "" },
			{ "Rescue", "rescue.brief_killed", "{killer} killed {assailant}", "for the remembered paragraph" },
			{ "Rescue", "rescue.state_down", "down", "fills {state}" },
			{ "Rescue", "rescue.state_deaths_door", "at death's door", "" },
			{ "Rescue", "rescue.healed_while", "While {name} was {state}, {healer} healed {name}{with}, {restoring}.", "{with} is empty or a space and heal.with" },
			{ "Rescue", "rescue.brief_healed", "{healer} healed {name}", "for the remembered paragraph" },
			{ "Rescue", "rescue.plain", "In plain terms: {saviors} saved {name}'s life in this battle. Without that help {name} would most likely have died.", "" },
			{ "Rescue", "rescue.none", "{name} survived that without anyone's rescue.", "" },
			{ "Rescue", "rescue.saved_other", "{name} saved {other}'s life.{deeds}", "{deeds} is the rescue's own sentences, each after a space" },

			{ "Healing", "heal.self", "{name} healed themself{with}, {restoring}.", "" },
			{ "Healing", "heal.other", "{healer} healed {name}{with}, {restoring}.", "" },
			{ "Healing", "heal.with", "with {source}", "" },
			{ "Healing", "heal.restoring", "restoring {amount} {name}'s health{points}", "{amount} is one of the three below" },
			{ "Healing", "heal.little", "a little of", "" },
			{ "Healing", "heal.good", "a good part of", "" },
			{ "Healing", "heal.most", "most or all of", "" },

			// ---------------------------------------------------------------- injuries, magic, friendly fire
			{ "Items", "items.potion", "{name} drank a potion in the fight: {items}.", "" },
			{ "Items", "items.potions", "{name} drank {count} potions in the fight: {items}.", "{items} reads \"Potion of Healing x3 and Potion of Fortify Two-handed\"" },
			{ "Items", "items.poison", "{name} used a poison in the fight: {items}.", "" },
			{ "Items", "items.poisons", "{name} used {count} poisons in the fight: {items}.", "" },
			{ "Items", "other.potion", "drank a potion", "for a companion's line" },
			{ "Items", "other.potions", "drank {count} potions", "" },
			{ "Items", "other.poison", "used a poison", "" },
			{ "Items", "other.poisons", "used {count} poisons", "" },

			{ "Injuries", "injury.carried", "{name} went into this fight already carrying: {injuries}.", "" },
			{ "Injuries", "injury.new", "{name} was injured in this fight and still carries it: {injuries}.", "" },
			{ "Injuries", "injury.memory", "{name} came out of it injured: {injuries}.", "for the remembered paragraph" },
			{ "Injuries", "injury.dealt_by", "{injury} (dealt by {name})", "" },
			{ "Injuries", "injury.unnamed", "an injury", "an injury effect with no name of its own" },
			{ "Injuries", "injury.minor", "a minor injury", "an injury mod's injury listed by what carries it (sInjurySources), marked minor" },
			{ "Injuries", "injury.serious", "a serious injury", "the same, unmarked" },
			{ "Injuries", "injury.lasting", "a lasting injury", "the same, for one marked as lasting" },

			{ "Effects", "effects.suffered", "{name} suffered: {effects}.", "{effects} reads \"burned by fire by Bandit Mage (Flames)\"" },
			{ "Effects", "effects.helped", "{name} was helped by: {effects}.", "" },
			{ "Effects", "effects.more", "and {count} more", "" },
			{ "Effects", "effect.by", "by {name}", "" },
			{ "Effects", "effect.own", "own {source}", "a spell they cast on themself: \"(own Stoneflesh)\"" },

			{ "FriendlyFire", "friendly.line", "{name} was caught in their own side's attacks, by accident (friendly fire, not an attack on them): {what}.", "" },
			{ "FriendlyFire", "friendly.crossfire_line", "{name} was caught in the crossfire, by accident (not an attack on them): {what}.",
				"a bystander who fought nobody, struck by the player's side during a fight" },
			{ "FriendlyFire", "friendly.damage", "took part of their damage from {names}", "" },

			// ---------------------------------------------------------------- kills and moments
			{ "Kills", "kills.mine", "{name} killed {count} of the {total} who died on the other side{victims}.", "{victims} is empty or a colon and the names" },
			{ "Kills", "kills.through", "{count} of them through {summons}.", "the part of those kills their summoned creatures made" },
			{ "Kills", "kills.unseen", "{count} of them without being seen.", "kills of victims who never saw their killer (a sneak attack)" },
			{ "Kills", "kills.dispatched", "{name} also dispatched {victims}.", "summoned creatures of the other side" },
			{ "Kills", "kills.none", "{name} killed none of the {total} who died on the other side.", "" },

			{ "Moments", "moment.at_once", "{killer} killed {count} at once{with}: {victims}.", "{with} is empty or a space and moment.with" },
			{ "Moments", "moment.with", "with {means}", "the spell or weapon" },
			{ "Moments", "maim.beheaded", "{killer} beheaded {victims}.", "" },
			{ "Moments", "maim.dismembered", "{killer} dismembered {victims}, severing a limb.", "" },
			{ "Moments", "maim.head_unknown", "{victims} lost their head in the fighting.", "" },
			{ "Moments", "maim.limb_unknown", "{victims} lost a limb in the fighting.", "" },

			// ---------------------------------------------------------------- the others, in one line each
			{ "Others", "other.line", "{name}: {facts}.", "{facts} are joined with semicolons" },
			{ "Others", "other.saved_by", "was saved by {saviors}", "" },
			{ "Others", "other.already_low", "went in already close to death from earlier wounds", "" },
			{ "Others", "other.no_danger", "was never near death", "" },
			{ "Others", "other.injured", "was injured ({injuries})", "" },
			{ "Others", "other.kills_none", "killed none of the other side", "" },
			{ "Others", "other.kills", "killed {count} of the other side", "" },
			{ "Others", "other.dispatched", "dispatched {victims}", "summoned creatures of the other side" },
			{ "Others", "other.easy", "None of them was seriously hurt or in any real danger.", "an easy fight: said in place of a line for each" },

			{ "Memory", "memory.rescued", "{name} {brink}, and would most likely have died, but {deeds}: {saviors} saved {name}'s life.", "the remembered paragraph" },
			{ "Memory", "memory.survived", "{name} {brink}.", "{brink} ends with how they came back from it (brink.got_up, brink.survived, brink.rose)" },

			// ---------------------------------------------------------------- what a magic effect is called
			{ "EffectNames", "fx.healed", "healed", "" },
			{ "EffectNames", "fx.paralyzed", "paralyzed", "" },
			{ "EffectNames", "fx.fear", "struck with magical fear", "" },
			{ "EffectNames", "fx.frenzy", "driven into a frenzy", "" },
			{ "EffectNames", "fx.calm", "magically calmed", "" },
			{ "EffectNames", "fx.staggered", "staggered", "" },
			{ "EffectNames", "fx.disarmed", "disarmed", "" },
			{ "EffectNames", "fx.turned", "turned and driven off", "" },
			{ "EffectNames", "fx.soul_trapped", "soul trapped", "" },
			{ "EffectNames", "fx.banished", "banished", "" },
			{ "EffectNames", "fx.invisible", "turned invisible", "" },
			{ "EffectNames", "fx.emboldened", "emboldened", "" },
			{ "EffectNames", "fx.ethereal", "made ethereal", "" },
			{ "EffectNames", "fx.cloak_hostile", "caught in a damaging cloak", "" },
			{ "EffectNames", "fx.cloak", "wrapped in a magical cloak", "" },
			{ "EffectNames", "fx.poisoned", "poisoned", "" },
			{ "EffectNames", "fx.diseased", "infected with disease", "" },
			{ "EffectNames", "fx.slowed", "slowed", "" },
			{ "EffectNames", "fx.burned", "burned by fire", "" },
			{ "EffectNames", "fx.frozen", "frozen by frost", "" },
			{ "EffectNames", "fx.shocked", "shocked by lightning", "" },
			{ "EffectNames", "fx.drained_life", "drained of life", "" },
			{ "EffectNames", "fx.hurt", "hurt by a harmful effect", "harm to health the mod cannot place: a spell, a bleed, a bite; it does not guess which" },
			{ "EffectNames", "fx.drained_stamina", "drained of stamina", "" },
			{ "EffectNames", "fx.drained_magicka", "drained of magicka", "" },
			{ "EffectNames", "fx.armor_weakened", "armor weakened", "" },
			{ "EffectNames", "fx.afflicted", "afflicted with {effect}", "a harmful effect the mod cannot place, by its own name" },
			{ "EffectNames", "fx.warded", "warded", "" },
			{ "EffectNames", "fx.armored", "shielded by magical armor", "" },
			{ "EffectNames", "fx.resist_fire", "protected against fire", "" },
			{ "EffectNames", "fx.resist_frost", "protected against frost", "" },
			{ "EffectNames", "fx.resist_shock", "protected against shock", "" },
			{ "EffectNames", "fx.resist_magic", "protected against magic", "" },
			{ "EffectNames", "fx.resist_poison", "protected against poison", "" },
			{ "EffectNames", "fx.resist_disease", "protected against disease", "" },
			{ "EffectNames", "fx.health_fortified", "health fortified", "" },
			{ "EffectNames", "fx.stamina_restored", "stamina restored", "" },
			{ "EffectNames", "fx.magicka_restored", "magicka restored", "" },
			{ "EffectNames", "fx.aided", "aided with {effect}", "a helpful effect the mod cannot place, by its own name" },
		};

		const std::unordered_map<std::string_view, const Entry*>& Index()
		{
			static const auto index = [] {
				std::unordered_map<std::string_view, const Entry*> m;
				for (const auto& e : kDefaults) m.emplace(e.key, &e);
				return m;
			}();
			return index;
		}

		std::shared_mutex                            g_lock;
		std::unordered_map<std::string, std::string> g_overrides;

		// the {names} in a wording, in order
		std::vector<std::string_view> Placeholders(std::string_view a_text)
		{
			std::vector<std::string_view> out;
			for (std::size_t i = 0; i < a_text.size(); ++i) {
				if (a_text[i] != '{') continue;
				const auto end = a_text.find('}', i);
				if (end == std::string_view::npos) break;
				out.push_back(a_text.substr(i + 1, end - i - 1));
				i = end;
			}
			return out;
		}
	}

	const std::vector<Entry>& Defaults() { return kDefaults; }

	std::string Get(std::string_view a_key)
	{
		{
			std::shared_lock l{ g_lock };
			if (!g_overrides.empty()) {
				if (const auto it = g_overrides.find(std::string(a_key)); it != g_overrides.end()) return it->second;
			}
		}
		const auto it = Index().find(a_key);
		return it != Index().end() ? std::string(it->second->text) : std::string();
	}

	std::string Fill(std::string_view a_key, std::initializer_list<Arg> a_args)
	{
		const std::string text = Get(a_key);
		std::string       out;
		out.reserve(text.size() + 32);
		for (std::size_t i = 0; i < text.size(); ++i) {
			if (text[i] == '{') {
				const auto end = text.find('}', i);
				if (end != std::string::npos) {
					const std::string_view name{ text.data() + i + 1, end - i - 1 };
					bool                   found = false;
					for (const auto& [n, v] : a_args) {
						if (n == name) {
							out += v;
							found = true;
							break;
						}
					}
					if (found) {
						i = end;
						continue;
					}
				}
			}
			out += text[i];
		}
		return out;
	}

	bool Accepts(std::string_view a_key, std::string_view a_text, std::string* a_why)
	{
		const auto it = Index().find(a_key);
		if (it == Index().end()) {
			if (a_why) *a_why = "there is no phrase of that name";
			return false;
		}
		const auto known = Placeholders(it->second->text);
		for (const auto p : Placeholders(a_text)) {
			bool ok = false;
			for (const auto k : known) ok = ok || k == p;
			if (!ok) {
				if (a_why) *a_why = "it uses {" + std::string(p) + "}, which this phrase does not have";
				return false;
			}
		}
		return true;
	}

	std::size_t SetOverrides(const std::vector<std::pair<std::string, std::string>>& a_lines)
	{
		std::unordered_map<std::string, std::string> taken;
		for (const auto& [key, text] : a_lines) {
			if (!text.empty() && Accepts(key, text)) taken[key] = text;
		}
		const auto       n = taken.size();
		std::unique_lock l{ g_lock };
		g_overrides = std::move(taken);
		return n;
	}

	std::string Reference()
	{
		std::string s =
			"; Battle Summaries: every piece of text the mod writes, with its built-in wording.\n"
			";\n"
			"; THIS FILE IS A REFERENCE. It is replaced whenever the mod is updated, so do not edit it.\n"
			"; To change a phrase, create BattleSummaries_Text.ini next to this file and copy into it only the lines you\n"
			"; want to change. That file is yours: the mod never ships or overwrites it.\n"
			";\n"
			"; - Words in {braces} are filled in by the mod. Keep the ones you need; you may leave one out, but you cannot\n"
			";   use one that the phrase does not have (such a line is ignored and the built-in wording is used).\n"
			"; - A {placeholder} that can be empty brings its own leading space or punctuation: write \"fought{at_place}\",\n"
			";   not \"fought {at_place}\".\n"
			"; - A missing or empty line means the built-in wording.\n"
			"; - Your file is read again whenever a battle begins, so a change can be tried without restarting the game.\n"
			";   BattleSummaries.log says how many of your lines were taken and names any it refused.\n"
			"; - The headings in [brackets] are only for finding things: a line works under any heading.\n";
		std::string_view group;
		for (const auto& e : kDefaults) {
			if (e.group != group) {
				group = e.group;
				s += "\n[";
				s += group;
				s += "]\n";
			}
			if (!e.note.empty()) {
				s += "; ";
				s += e.note;
				s += "\n";
			}
			s += e.key;
			s += " = ";
			s += e.text;
			s += "\n";
		}
		return s;
	}
}
