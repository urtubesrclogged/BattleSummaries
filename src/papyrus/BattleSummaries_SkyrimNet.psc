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
EndFunction

; One remembered event per battle worth remembering: the whole battle in a paragraph, known to those who were there.
Function Remember(String asText) Global
	Int r = SkyrimNetApi.RegisterPersistentEvent(asText, None, Game.GetPlayer())
	BattleSummaries_Native.ReportRemembered(r)   ; into BattleSummaries.log: 0 = accepted
EndFunction
