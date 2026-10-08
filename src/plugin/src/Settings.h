#pragma once

#include <string>
#include <vector>

namespace BSM::Settings
{
	struct Data
	{
		bool  enabled{ true };
		float nearDeathPct{ 0.15f };    // below this share of health an actor counts as near death
		float endGraceSeconds{ 6.0f };  // the battle is over once nobody in it has been in combat for this long
		float mergeGapSeconds{ 20.0f }; // fighting that resumes within this long continues the same battle
		float summaryGameHours{ 6.0f };   // how long after a battle its summary is still given to dialogue
		float range{ 10000.0f };        // actors further than this from the player are not tracked
		float witnessRange{ 3500.0f };  // bystanders this close at the end know what happened
		bool  showNumbers{ false };
		bool  ignoreFriendlyFire{ false };  // false: harm from one's own side is told as an accident; true: left out
		int   maxOthers{ 6 };
		int   maxEffects{ 6 };

		bool  rememberEvent{ true };    // register one persistent SkyrimNet event per significant battle
		int   minEnemies{ 3 };
		float minSeconds{ 30.0f };

		// what other mods add
		bool                     injuries{ true };       // tell injuries from injury mods
		std::vector<std::string> injuryKeywords{ "MAG_MagicInjurySpell", "WCT_Injury_Keyword" };  // Blade and Blunt, Wildcat
		bool                     dismemberment{ true };  // tell beheadings and severed limbs

		bool verbose{ false };
	};

	[[nodiscard]] const Data& Get();
	void                      Load();  // Data/SKSE/Plugins/BattleSummaries.ini; missing keys keep their defaults
	void                      SetVerbose(bool a_on);

	// A setting made on SkyrimNet's own settings page (its path there, its value as text). Empty = not set there:
	// the ini's value stands. Main thread.
	void Override(const std::string& a_path, const std::string& a_value);
}
