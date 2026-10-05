# Changelog

## 1.0.2

- The mod's SkyrimNet content now has the same id as its page on the SkyrimNet plugin hub
  (`urtubesrclogged.battle-summaries`; it was `urtubesrclogged.battlesummaries`). Nothing changes in game.
- **When updating, replace the old version; do not merge into it.** A merge leaves the earlier content folder
  (`SKSE/Plugins/SkyrimNet/external/urtubesrclogged.battlesummaries`) beside the new one, and every summary would
  then be given to NPCs twice. If that happened, delete the old folder.

## 1.0.1

- Fixed a crash to desktop when the player tried to use furniture that someone else was already sitting on. The
  game calls the function this mod hooks for that check too, with a value that is not an actor; the hook no longer
  reads through it.
- A battle is now the party, those who fought the party, and those who fought them. On creature-heavy load orders
  every predator and its prey near a fight used to be listed as enemies and credited with kills.
- A fighter who walked into a battle already close to death is told so, not described as brought there by this
  battle.
- Magic-effect events are ignored while no battle is on and during loading, where the game reports thousands.
- Also run on Skyrim SE 1.5.97 (it loads, hooks and records battles there); so far only Skyrim VR had been tried.

## 1.0.0

First public release.

- Records each battle near the player: sides, deaths and killers, damage taken and dealt, bleedouts and close calls,
  rescues, harmful and helpful magic and who cast it.
- Gives each NPC a summary from their own point of view in their own SkyrimNet prompt, during the battle and for six
  game hours after it; bystanders who stood close get a witness account.
- Registers one remembered SkyrimNet event per battle worth remembering.
- Skyrim SE, AE and VR from one DLL; no plugin, nothing stored in the save.
