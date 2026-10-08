#include "Settings.h"

#include <SimpleIni.h>

namespace BSM::Settings
{
	namespace
	{
		Data g_data;
	}

	const Data& Get() { return g_data; }

	void SetVerbose(bool a_on)
	{
		g_data.verbose = a_on;
		spdlog::set_level(a_on ? spdlog::level::debug : spdlog::level::info);
		spdlog::flush_on(a_on ? spdlog::level::debug : spdlog::level::info);
	}

	void Override(const std::string& a_path, const std::string& a_value)
	{
		if (a_value.empty()) return;
		std::string v = a_value;
		std::ranges::transform(v, v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		const bool on = v == "true" || v == "1" || v == "yes" || v == "on";
		const auto set = [&](bool& a_field) {
			if (a_field != on) SKSE::log::info("Settings: {} = {} (from SkyrimNet's settings page)", a_path, on);
			a_field = on;
		};
		if (a_path == "summary.ignore_friendly_fire") set(g_data.ignoreFriendlyFire);
		else if (a_path == "summary.show_numbers") set(g_data.showNumbers);
		else if (a_path == "memory.remember_battles") set(g_data.rememberEvent);
		else if (a_path == "mods.injuries") set(g_data.injuries);
		else if (a_path == "mods.dismemberment") set(g_data.dismemberment);
		else if (a_path == "summary.game_hours") {
			float hours = g_data.summaryGameHours;
			try {
				hours = std::stof(v);
			} catch (...) {
				return;
			}
			hours = std::clamp(hours, 0.25f, 240.0f);
			if (std::fabs(hours - g_data.summaryGameHours) > 0.01f) SKSE::log::info("Settings: {} = {} (from SkyrimNet's settings page)", a_path, hours);
			g_data.summaryGameHours = hours;
		}
	}

	void Load()
	{
		Data      d;
		CSimpleIniA ini;
		ini.SetUnicode();
		const auto rc = ini.LoadFile(L"Data/SKSE/Plugins/BattleSummaries.ini");
		if (rc < 0) {
			SKSE::log::warn("BattleSummaries.ini not found; using defaults");
		} else {
			d.enabled = ini.GetBoolValue("General", "bEnabled", d.enabled);
			d.nearDeathPct = static_cast<float>(ini.GetDoubleValue("General", "fNearDeathHealthShare", d.nearDeathPct));
			d.endGraceSeconds = static_cast<float>(ini.GetDoubleValue("General", "fEndGraceSeconds", d.endGraceSeconds));
			d.mergeGapSeconds = static_cast<float>(ini.GetDoubleValue("General", "fMergeGapSeconds", d.mergeGapSeconds));
			d.range = static_cast<float>(ini.GetDoubleValue("General", "fTrackingRange", d.range));
			d.witnessRange = static_cast<float>(ini.GetDoubleValue("General", "fWitnessRange", d.witnessRange));

			d.summaryGameHours = static_cast<float>(ini.GetDoubleValue("Summary", "fSummaryGameHours", d.summaryGameHours));
			d.showNumbers = ini.GetBoolValue("Summary", "bShowNumbers", d.showNumbers);
			d.ignoreFriendlyFire = ini.GetBoolValue("Summary", "bIgnoreFriendlyFire", d.ignoreFriendlyFire);
			d.maxOthers = static_cast<int>(ini.GetLongValue("Summary", "iMaxCompanionLines", d.maxOthers));
			d.maxEffects = static_cast<int>(ini.GetLongValue("Summary", "iMaxEffectsPerList", d.maxEffects));

			d.rememberEvent = ini.GetBoolValue("Memory", "bRememberBattles", d.rememberEvent);
			d.minEnemies = static_cast<int>(ini.GetLongValue("Memory", "iMinEnemies", d.minEnemies));
			d.minSeconds = static_cast<float>(ini.GetDoubleValue("Memory", "fMinSeconds", d.minSeconds));

			d.injuries = ini.GetBoolValue("OtherMods", "bInjuries", d.injuries);
			if (const char* list = ini.GetValue("OtherMods", "sInjuryKeywords", nullptr)) {
				d.injuryKeywords.clear();
				std::string word;
				const auto  flush = [&] {
					if (!word.empty()) d.injuryKeywords.push_back(word);
					word.clear();
				};
				for (const char* c = list; *c; ++c) {
					if (*c == ',' || *c == ';' || std::isspace(static_cast<unsigned char>(*c))) flush();
					else word += *c;
				}
				flush();
			}
			d.dismemberment = ini.GetBoolValue("OtherMods", "bDismemberment", d.dismemberment);

			d.verbose = ini.GetBoolValue("Debug", "bVerboseLog", d.verbose);
		}
		d.nearDeathPct = std::clamp(d.nearDeathPct, 0.01f, 0.9f);
		d.endGraceSeconds = std::clamp(d.endGraceSeconds, 1.0f, 120.0f);
		g_data = d;
		SetVerbose(d.verbose);
		SKSE::log::info("Settings: injuries {} ({} keywords), dismemberment {}", d.injuries, d.injuryKeywords.size(), d.dismemberment);
		SKSE::log::info("Settings: enabled {}, near death below {:.0f}%, end grace {}s, merge gap {}s, summary for {} game hours, numbers {}, remember {}",
			d.enabled, d.nearDeathPct * 100.0f, d.endGraceSeconds, d.mergeGapSeconds, d.summaryGameHours, d.showNumbers, d.rememberEvent);
	}
}
