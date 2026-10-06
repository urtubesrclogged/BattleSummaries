#include "Effects.h"

#include "Settings.h"

namespace BSM::Effects
{
	namespace
	{
		using AV = RE::ActorValue;
		using Arch = RE::EffectSetting::Archetype;
		using Flag = RE::EffectSetting::EffectSettingData::Flag;

		bool ModifiesValue(Arch a_arch)
		{
			return a_arch == Arch::kValueModifier || a_arch == Arch::kPeakValueModifier || a_arch == Arch::kDualValueModifier || a_arch == Arch::kAbsorb ||
				   a_arch == Arch::kAccumulateMagnitude;
		}

		// Something a fighter casts, drinks, shouts or strikes with: the carriers whose effects are worth naming even when
		// this file cannot place them. Anything else (or no known carrier at all) is some mod's own machinery.
		bool Castable(const RE::MagicItem* a_source)
		{
			if (!a_source) return false;
			switch (a_source->GetSpellType()) {
			case RE::MagicSystem::SpellType::kSpell:
			case RE::MagicSystem::SpellType::kPower:
			case RE::MagicSystem::SpellType::kLesserPower:
			case RE::MagicSystem::SpellType::kVoicePower:
			case RE::MagicSystem::SpellType::kPoison:
			case RE::MagicSystem::SpellType::kPotion:
			case RE::MagicSystem::SpellType::kScroll:
			case RE::MagicSystem::SpellType::kStaffEnchantment:
			case RE::MagicSystem::SpellType::kEnchantment:
				return true;
			default:
				return false;
			}
		}

		const char* ResistLabel(AV a_av)
		{
			switch (a_av) {
			case AV::kResistFire: return "protected against fire";
			case AV::kResistFrost: return "protected against frost";
			case AV::kResistShock: return "protected against shock";
			case AV::kResistMagic: return "protected against magic";
			case AV::kPoisonResist: return "protected against poison";
			case AV::kResistDisease: return "protected against disease";
			default: return nullptr;
			}
		}
	}

	bool RestoresHealth(const RE::EffectSetting* a_effect)
	{
		if (!a_effect) return false;
		const auto& d = a_effect->data;
		return d.archetype == Arch::kValueModifier && d.primaryAV == AV::kHealth && d.flags.none(Flag::kDetrimental, Flag::kHostile, Flag::kRecover);
	}

	bool IsInjury(const RE::EffectSetting* a_effect)
	{
		const auto& cfg = Settings::Get();
		if (!a_effect || !cfg.injuries) return false;
		for (const auto& kw : cfg.injuryKeywords) {
			if (a_effect->HasKeywordString(kw)) return true;
		}
		return false;
	}

	std::string InjuryName(const RE::EffectSetting* a_effect, const RE::MagicItem* a_source)
	{
		const auto text = [](const char* a_s) { return a_s ? std::string(a_s) : std::string(); };
		const auto own = text(a_effect ? a_effect->GetFullName() : nullptr);
		const auto from = text(a_source ? a_source->GetFullName() : nullptr);
		const auto saysInjury = [](std::string a_s) {
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s.find("injur") != std::string::npos || a_s.find("wound") != std::string::npos;
		};
		if (saysInjury(own) || from.empty()) return own.empty() ? std::string("an injury") : own;
		return from;
	}

	std::optional<Kind> Classify(const RE::EffectSetting* a_effect, const RE::MagicItem* a_source, bool a_selfCast)
	{
		if (!a_effect) return std::nullopt;
		const auto& d = a_effect->data;
		const char* name = a_effect->GetFullName();
		// hidden and nameless effects are other mods' plumbing, not something a fighter would notice
		if (d.flags.any(Flag::kHideInUI) || !name || !*name) return std::nullopt;
		if (a_source) {
			const auto type = a_source->GetSpellType();
			if (type == RE::MagicSystem::SpellType::kAbility || type == RE::MagicSystem::SpellType::kAddiction) return std::nullopt;
			if (a_source->GetCastingType() == RE::MagicSystem::CastingType::kConstantEffect) return std::nullopt;  // worn enchantments
		}

		const auto arch = d.archetype;
		const auto av = d.primaryAV;
		const auto has = [&](std::string_view a_kw) { return a_effect->HasKeywordString(a_kw); };
		const bool bad = d.flags.any(Flag::kDetrimental, Flag::kHostile);

		if (RestoresHealth(a_effect)) return Kind{ "healed", false, true };

		switch (arch) {
		case Arch::kParalysis: return Kind{ "paralyzed", true };
		case Arch::kDemoralize: return Kind{ "struck with magical fear", true };
		case Arch::kFrenzy: return Kind{ "driven into a frenzy", true };
		case Arch::kCalm: return Kind{ "magically calmed", true };
		case Arch::kStagger: return Kind{ "staggered", true };
		case Arch::kDisarm: return Kind{ "disarmed", true };
		case Arch::kTurnUndead: return Kind{ "turned and driven off", true };
		case Arch::kSoulTrap: return Kind{ "soul trapped", true };
		case Arch::kBanish: return Kind{ "banished", true };
		case Arch::kInvisibility: return Kind{ "turned invisible", false };
		case Arch::kRally: return Kind{ "emboldened", false };
		case Arch::kEtherealize: return Kind{ "made ethereal", false };
		case Arch::kCloak: return bad && !a_selfCast ? Kind{ "caught in a damaging cloak", true } : Kind{ "wrapped in a magical cloak", false };
		// casting these is not something done to the target
		case Arch::kSummonCreature:
		case Arch::kReanimate:
		case Arch::kCommandSummoned:
		case Arch::kLight:
		case Arch::kScript:
		case Arch::kSlowTime:
			return std::nullopt;
		default:
			break;
		}

		if (bad) {
			const bool poison = d.resistVariable == AV::kPoisonResist || (a_source && a_source->IsPoison());
			if (poison) return Kind{ "poisoned", true };
			if (d.resistVariable == AV::kResistDisease) return Kind{ "infected with disease", true };
			if (has("MagicSlow") || av == AV::kSpeedMult) return Kind{ "slowed", true };
			if (has("MagicParalysis")) return Kind{ "paralyzed", true };
			if (has("MagicDamageFire") || d.resistVariable == AV::kResistFire) return Kind{ "burned by fire", true };
			if (has("MagicDamageFrost") || d.resistVariable == AV::kResistFrost) return Kind{ "frozen by frost", true };
			if (has("MagicDamageShock") || d.resistVariable == AV::kResistShock) return Kind{ "shocked by lightning", true };
			if (ModifiesValue(arch)) {
				if (arch == Arch::kAbsorb) return Kind{ "drained of life", true };
				if (av == AV::kHealth) return Kind{ "hurt by harmful magic", true };
				if (av == AV::kStamina) return Kind{ "drained of stamina", true };
				if (av == AV::kMagicka) return Kind{ "drained of magicka", true };
				if (av == AV::kDamageResist) return Kind{ "armor weakened", true };
			}
			// unplaced: named by the effect itself, when a spell, poison or weapon carried it
			if (!Castable(a_source)) return std::nullopt;
			return Kind{ std::format("afflicted with {}", name), true };
		}

		if (has("MagicWard")) return Kind{ "warded", false };
		if (has("MagicArmorSpell") || (ModifiesValue(arch) && av == AV::kDamageResist)) return Kind{ "shielded by magical armor", false };
		if (ModifiesValue(arch)) {
			if (const auto* r = ResistLabel(av)) return Kind{ r, false };
			if (av == AV::kHealth) return Kind{ "health fortified", false };
			if (av == AV::kStamina) return Kind{ "stamina restored", false };
			if (av == AV::kMagicka) return Kind{ "magicka restored", false };
		}
		// an unplaced helpful effect: worth a line when someone else gave it, noise when it is the caster's own
		if (a_selfCast || !Castable(a_source)) return std::nullopt;
		return Kind{ std::format("aided with {}", name), false };
	}
}
