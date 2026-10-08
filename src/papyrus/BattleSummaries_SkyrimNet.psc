ScriptName BattleSummaries_SkyrimNet Hidden
{SkyrimNet side of Battle Summaries. Called by BattleSummaries.dll, and only when SkyrimNet is installed; not attached to any form.}

; {{ battle_summary(npc.UUID) }} -> see BattleSummaries_Native.GetSummaryJson
String Function GetSummary(Actor akActor) Global
	Return BattleSummaries_Native.GetSummaryJson(akActor)
EndFunction

; After every game load.
Function Register() Global
	Int r = SkyrimNetApi.RegisterDecorator("battle_summary", "BattleSummaries_SkyrimNet", "GetSummary")
	BattleSummaries_Native.ReportRegistered(r)   ; into BattleSummaries.log: 0 = registered
	ReadSettings()
EndFunction

; What the player set on SkyrimNet's settings page (config/plugins/BattleSummaries/manifest.yaml lists the fields).
; Called after every game load and when a battle begins. A value that comes back empty was never set there, and
; the ini's value stands.
Function ReadSettings() Global
	String c = "Plugin_BattleSummaries"
	String[] paths = new String[6]
	paths[0] = "summary.ignore_friendly_fire"
	paths[1] = "summary.show_numbers"
	paths[2] = "summary.game_hours"
	paths[3] = "memory.remember_battles"
	paths[4] = "mods.injuries"
	paths[5] = "mods.dismemberment"
	Int i = 0
	While i < paths.Length
		BattleSummaries_Native.ApplySetting(paths[i], SkyrimNetApi.GetConfigString(c, paths[i], ""))
		i += 1
	EndWhile
EndFunction

; One remembered event per battle worth remembering: the whole battle in a paragraph, known to those who were there.
Function Remember(String asText) Global
	Int r = SkyrimNetApi.RegisterPersistentEvent(asText, None, Game.GetPlayer())
	BattleSummaries_Native.ReportRemembered(r)   ; into BattleSummaries.log: 0 = accepted
EndFunction
