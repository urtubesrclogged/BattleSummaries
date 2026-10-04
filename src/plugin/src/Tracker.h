#pragma once

// Watches the game and keeps the Battle up to date. Everything the game reports (combat state, damage, deaths,
// bleedouts, magic effects) is queued where it happens and worked through on the main thread a few times a second,
// so the model is only ever touched from there and from the Papyrus thread that reads a summary.

namespace BSM::Tracker
{
	void Install();  // once, at kDataLoaded: event sinks, the health-damage hook, the tick
	void Reset();    // a save was loaded or a new game started: forget everything

	[[nodiscard]] std::string SummaryJson(RE::Actor* a_viewer);  // what the battle_summary decorator returns
	[[nodiscard]] std::string SummaryPlain(RE::Actor* a_viewer);
	[[nodiscard]] std::string Status();
	[[nodiscard]] std::string LastMemory();
	void                      EndNow();

	// Test aid: an already-fought battle in which a_victim was struck down by a bandit chief and saved by the player.
	// Lets the SkyrimNet side be checked without a fight.
	[[nodiscard]] std::string FakeRescue(RE::Actor* a_victim);
}
