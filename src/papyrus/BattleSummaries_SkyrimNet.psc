ScriptName BattleSummaries_SkyrimNet Hidden
{SkyrimNet side of Battle Summaries. Called by BattleSummaries.dll, and only when SkyrimNet is installed; not attached to any form.}

; {{ battle_summary(npc.UUID) }} -> see BattleSummaries_Native.GetSummaryJson
String Function GetSummary(Actor akActor) Global
	Return BattleSummaries_Native.GetSummaryJson(akActor)
EndFunction

; After every game load.
Function Register() Global
	Int r = SkyrimNetApi.RegisterDecorator("battle_summary", "BattleSummaries_SkyrimNet", "GetSummary")
	Debug.Trace("[BattleSummaries] SkyrimNet decorator battle_summary registered: " + (r == 0) as String)
EndFunction

; One remembered event per battle worth remembering: the whole battle in a paragraph, known to those who were there.
Function Remember(String asText) Global
	SkyrimNetApi.RegisterPersistentEvent(asText, None, Game.GetPlayer())
EndFunction
