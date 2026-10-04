#pragma once

// Reads a magic effect the way a fighter would describe it afterwards: "burned by fire", "slowed", "shielded by
// magical armor". Works from the effect's own data (archetype, actor value, resistance, keywords), so spells from
// other mods classify too; anything it cannot place is named by the effect itself.

namespace BSM::Effects
{
	struct Kind
	{
		std::string label;
		bool        hostile{ false };
		bool        heal{ false };  // restores health: told through the healing lines, with the amount
	};

	// a_source: the spell, potion, poison or enchantment that carried the effect, when known.
	[[nodiscard]] std::optional<Kind> Classify(const RE::EffectSetting* a_effect, const RE::MagicItem* a_source, bool a_selfCast);

	[[nodiscard]] bool RestoresHealth(const RE::EffectSetting* a_effect);
}
