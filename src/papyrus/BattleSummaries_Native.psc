ScriptName BattleSummaries_Native Native Hidden
{Native bridge to BattleSummaries.dll (SKSE). The DLL tracks each battle near the player; these read what it knows.}

; What akActor should know about the battle underway or the last one fought, as JSON:
; {"show":true,"ongoing":false,"participant":true,"name":"Lydia","age":"moments ago",
;  "general":["A battle was fought at ...", ...],"personal":["Lydia was struck down by ...", ...],"others":["Kaira: ...", ...]}
; show is false when there is nothing to tell (no battle, too long ago, or akActor neither fought in it nor saw it).
String Function GetSummaryJson(Actor akActor) Global Native

; ---- for testing and support ----
String Function GetSummaryText(Actor akActor) Global Native   ; the same summary as plain text
String Function GetStatus() Global Native                   ; the battle underway, with each participant's running totals
String Function GetPerformance() Global Native              ; what the mod has cost since the game started: calls, average, worst
Function ResetPerformance() Global Native                   ; starts that measurement afresh
String Function GetLastMemory() Global Native              ; the remembered paragraph for the last battle
String Function GetVersion() Global Native
Function EndBattleNow() Global Native                       ; closes the battle underway without waiting for the grace period
Function ReloadSettings() Global Native                     ; re-reads BattleSummaries.ini
Function SetVerboseLog(Bool abOn) Global Native             ; per-event lines in BattleSummaries.log
; Records an invented battle in which akVictim was struck down by a bandit chief and saved by the player, so the
; SkyrimNet side can be checked without a fight. Never written to SkyrimNet's memory.
String Function DebugFakeRescue(Actor akVictim) Global Native
