# Battle Summaries for SkyrimNet

After a fight, SkyrimNet NPCs improvise what happened: the follower you dragged back from bleedout tells you that you
were sloppy and that she never needed your help. SkyrimNet keeps raw hit, spell and combat events out of the dialogue
history (for good reason: there are hundreds of them per fight), so the dialogue model has nothing to go on.

Battle Summaries records each battle near the player and gives every NPC a short, factual account of it in place of
those raw events, from that NPC's own point of view:

- who fought on which side, who died, who survived
- how badly each was hurt and how much of the fighting each did
- who was struck down (bleedout) or came close to death (below 15% health), and by whose blows
- who saved whom: who killed the attacker or did the healing after the victim was in danger
- harmful magic put on them and by whom (burned, frozen, shocked, poisoned, slowed, paralyzed, drained, ...)
- help given to them and by whom (healing, magical armor, wards, resistances, courage, ...)
- kills per fighter, the player's set apart from the NPC's own
- one paragraph summing up the whole battle, remembered as a SkyrimNet event

NPCs are told they may feel anything about the battle that fits their character, but must not deny or reverse what
happened or claim deeds that are not theirs.

## Requirements

- [SkyrimNet](https://github.com/MinLL/SkyrimNet-GamePlugin) beta 25 (0.25.0) or later, configured and working
- SKSE64 or SKSE VR
- Address Library for SKSE Plugins (Skyrim VR: VR Address Library for SKSEVR)

One DLL for Skyrim SE, AE and VR. No plugin (.esp) and nothing stored in the save, so it can be added or removed at
any time.

Developed and tested on Skyrim VR. SE and AE are built from the same code but have not been tested yet: reports are
welcome.

## Installing

Install the release zip with your mod manager and enable it. There is no plugin to place in the load order. Start the
game: SkyrimNet picks the content up by itself (it shows as "Battle Summaries" among its plugins).

## How it works

`BattleSummaries.dll` listens to the game's combat, death, bleedout and magic-effect events and hooks
`Actor::HandleHealthDamage`, the one place every loss of health passes through with its attacker and its amount.
A battle begins with the first combat state or blow involving the player's side and ends once nobody in it has been in
combat for a few seconds; fighting that flares up again shortly after continues the same battle. Bystanders and
wildlife that never traded a blow with either side are left out.

Two things reach SkyrimNet:

1. **The summary** - the decorator `battle_summary(npc.UUID)`, rendered by
   `prompts/submodules/user_final_instructions/0210_battle_summary.prompt` into the speaker's own prompt while the
   battle is underway and for six game hours after it. Bystanders who stood close enough get a shorter witness
   account. Edit that file to change the framing or the closing rule.
2. **The memory** - one persistent event per battle worth remembering (someone on the player's side fell, went down
   or nearly died; or three or more enemies; or half a minute of fighting), so the battle stays in event history and
   memories after the summary has expired.

No extra LLM calls are made: the summary is built from what the game recorded.

Amounts are told in words ("took solid wounds", "dealt most of their side's damage") because language models recite
any figure they are given. Long lists of names are cut short, and an easy fight is told in a few lines.

## Settings

`SKSE/Plugins/BattleSummaries.ini`, read at game start:

| Setting | Default | Meaning |
|---|---|---|
| `[General] bEnabled` | true | Master switch |
| `fNearDeathHealthShare` | 0.15 | Below this share of health an actor counts as having come close to death |
| `fEndGraceSeconds` | 6 | The battle is over once nobody in it has been in combat for this long |
| `fMergeGapSeconds` | 20 | Fighting that starts again within this long continues the same battle |
| `fTrackingRange` | 10000 | Actors further than this from the player (game units) are not tracked |
| `fWitnessRange` | 3500 | Bystanders this close when a battle ends know what happened in it |
| `[Summary] fSummaryGameHours` | 6 | How long after a battle its summary is still given to dialogue |
| `bShowNumbers` | false | Add raw damage and healing points to the summary |
| `iMaxCompanionLines` | 6 | Most companions listed one by one |
| `iMaxEffectsPerList` | 6 | Most magic effects listed per fighter |
| `[Memory] bRememberBattles` | true | Register a remembered SkyrimNet event per battle worth remembering |
| `iMinEnemies` / `fMinSeconds` | 3 / 30 | What makes a battle worth remembering when nobody was in danger |
| `[Debug] bVerboseLog` | false | One log line per damage, death, bleedout, effect and heal |

## Performance

The mod measures its own cost. Over an eight-minute dungeon run on Skyrim VR with four battles of up to eleven
fighters and the verbose log on, all of its work together came to 46 milliseconds, about 0.01% of one CPU core; the
slowest single step took 0.38 ms. Read the numbers from your own game with `BattleSummaries_Native.GetPerformance()`.

## Troubleshooting

`BattleSummaries.log` is written next to the other SKSE logs (`Documents/My Games/<Skyrim>/SKSE/`). It records each
battle as it begins and ends, with the paragraph that was remembered.

Papyrus functions for support and testing (`BattleSummaries_Native`):

- `GetStatus()` - the battle underway and the last three, with each participant's figures
- `GetSummaryText(actor)` - exactly what that actor would be told
- `GetLastMemory()` - the paragraph for the last battle
- `GetPerformance()` / `ResetPerformance()` - what the mod has cost since the game started
- `SetVerboseLog(true)` - per-event lines in the log for this session
- `DebugFakeRescue(actor)` - records an invented battle in which that actor was struck down and saved by the
  player, to try the dialogue side without a fight; never written to SkyrimNet's memory

## Building

Needs Visual Studio 2022 Build Tools, CMake, vcpkg, a [CommonLibVR](https://github.com/alandtse/CommonLibVR) (ng
branch) checkout and the [Caprica](https://github.com/Orvid/Caprica) Papyrus compiler.

```
copy local.env.example local.env      (set your paths)
powershell -File build.ps1            (Papyrus, offline tests, DLL)
bash deploy.sh                        (into the mod folders named in local.env; refuses while the game runs)
bash package.sh                       (release/Battle Summaries <version>.zip)
```

The battle model and its narrative (`src/plugin/src/Core.*`, `Narrative.*`) use no game types; `tests/tests.cpp`
replays fights against them and checks what each NPC is told. `build.ps1` runs those tests before it builds the DLL.

## License

Two parts: the code (the SKSE plugin, the Papyrus scripts, the ini, the build scripts) is MIT; the SkyrimNet content
(the manifest and the prompt in the mod's SkyrimNet layer) is under the SkyrimNet Plugin License 1.0, so that content
may be changed for your own game but shared only through the SkyrimNet plugin hub. See `LICENSE`,
`LICENSE-CONTENT.md` and `THIRD_PARTY_NOTICES.md`.

Battle Summaries is an independent add-on and not part of SkyrimNet.
