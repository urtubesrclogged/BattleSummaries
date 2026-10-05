# Changelog

## 1.0.1

- Fixed a crash to desktop when the player tried to use furniture that someone else was already sitting on. The
  game calls the function this mod hooks for that check too, with a value that is not an actor; the hook no longer
  reads through it.

## 1.0.0

First public release.

- Records each battle near the player: sides, deaths and killers, damage taken and dealt, bleedouts and close calls,
  rescues, harmful and helpful magic and who cast it.
- Gives each NPC a summary from their own point of view in their own SkyrimNet prompt, during the battle and for six
  game hours after it; bystanders who stood close get a witness account.
- Registers one remembered SkyrimNet event per battle worth remembering.
- Skyrim SE, AE and VR from one DLL; no plugin, nothing stored in the save.
