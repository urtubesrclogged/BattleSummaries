#pragma once

// Turns a Battle into what an NPC should know about it: a few general lines, what happened to that NPC, and a line
// for each of their companions. All of it is built from recorded facts; nothing here guesses beyond "who saved whom",
// which is derived from who killed the assailant or healed the victim after the victim was near death.

#include "Core.h"

namespace BSM
{
	struct NarrativeOptions
	{
		bool showNumbers{ false };  // add raw damage / healing points; off by default, since models tend to recite figures
		int  maxOthers{ 6 };
		int  maxEffects{ 6 };
		// Friendly fire (harm from someone on the same side) is told as an accident, or left out of the summary
		// altogether. A death by friendly fire is always told.
		bool ignoreFriendlyFire{ false };
	};

	struct Summary
	{
		bool                     show{ false };
		bool                     ongoing{ false };
		bool                     participant{ false };  // false: the viewer only witnessed it
		std::string              name;                  // the viewer's name
		std::vector<std::string> general;
		std::vector<std::string> personal;
		std::vector<std::string> others;
	};

	// a_viewer may be an actor who is not in the battle (a witness): they get the general lines and the others.
	[[nodiscard]] Summary BuildSummary(const Battle& a_battle, ActorId a_viewer, const std::string& a_viewerName, const NarrativeOptions& a_opt);

	// One neutral paragraph for the whole battle (the remembered event).
	[[nodiscard]] std::string BuildMemory(const Battle& a_battle, const NarrativeOptions& a_opt);

	// How long a battle lasted, as told: seconds up to a minute, whole minutes up to five, the nearest five minutes up
	// to an hour, the nearest half hour beyond ("42 seconds", "3 minutes", "about 25 minutes", "about 1.5 hours").
	[[nodiscard]] std::string DurationText(double a_seconds);

	// The summary as plain text, for logs and debugging.
	[[nodiscard]] std::string SummaryText(const Summary& a_summary);

	// Worth remembering as an event of its own: more than a passing scuffle.
	[[nodiscard]] bool Significant(const Battle& a_battle, int a_minEnemies, double a_minSeconds);
}
