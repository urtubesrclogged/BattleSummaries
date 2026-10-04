#pragma once

// The way into SkyrimNet: its Papyrus API (SkyrimNetApi), reached through BattleSummaries_SkyrimNet.psc. Without
// SkyrimNet installed nothing here does anything.

namespace BSM::SkyrimNet
{
	[[nodiscard]] bool Available();                          // SkyrimNet.dll is loaded
	void               Register();                           // registers the battle_summary decorator; after every game load
	void               Remember(const std::string& a_text);  // one persistent event: a battle worth remembering
}
