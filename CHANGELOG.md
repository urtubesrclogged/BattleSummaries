# Changelog

## 1.2.0

**Friendly fire**
- Caught your follower in a fireball? It's now told as an accident, not as an attack on them.
- Or switch it off entirely and they'll stop bringing it up. (A death by friendly fire is always told.)

**Settings in SkyrimNet**
- Battle Summaries now has its own page in SkyrimNet's settings, under Plugins. The ini still works.

**Moments**
- Several enemies killed in one stroke get a mention, with what did it: "killed 3 at once with Fireball".

**Fixes**
- An enemy who happens to kill another enemy no longer "saves" your follower's life.
- Damage done to your own side no longer counts as doing the fighting.
- "Killed by someone" is gone.

## 1.1.0

- **Injuries from injury mods.** A fighter who picks up an injury during the battle is told so, with whose blows did it
  when one landed just before ("was injured in this fight and still carries it: Leg Injury (dealt by Bandit Chief)");
  one they already had is told as brought into the fight. Works out of the box with Blade and Blunt and Wildcat. Any
  other injury mod that marks its injuries with a keyword can be added in the ini (`[OtherMods] sInjuryKeywords`).
- **Beheadings and severed limbs.** The game's own beheadings, and what Dismembering Framework (limbs) and Next-Gen
  Decapitations (heads) do when they are installed, are told with the kill ("Lydia beheaded Bandit Chief."). None of them is required.
- Both can be switched off in the ini (`bInjuries`, `bDismemberment`).
- **Innocents and allies slain.** A citizen or guard who was cut down without ever raising a weapon is no longer
  listed among the enemies: they are told as an innocent, with who killed them. An ally killed by their own side is
  told as that, accident or not. Either makes the battle one to remember.
- **Kill credit, again.** The game also names the player for deaths the player had no part in (a follower felled by a
  giant was told as slain by the player). Its word is now taken only when the one it names struck the victim.
- **A battle ends with its enemies.** The game can keep the party "in combat" for minutes after the last enemy fell (a
  follower chasing a deer); the battle used to stay open, and be told as underway, for as long.
- **How long the battle lasted** is told as a figure: seconds up to a minute, minutes up to five, the nearest five
  minutes up to an hour, the nearest half hour beyond ("It lasted 42 seconds.", "It lasted about 25 minutes."). Time spent in menus is not counted.

## 1.0.2

- **Kills are credited to whoever dealt the final blow.** The game's own death report names the player for many kills
  their followers make, so a player who only healed was told (and so were the NPCs) that they had killed everyone.
- `BattleSummaries.log` now says whether SkyrimNet accepted the decorator and each remembered event, when SkyrimNet
  first asks for a summary, and which NPCs were given one: enough to tell from a log whether the SkyrimNet side works.
- Open country is named correctly in translated games ("the wilds of ..." used to appear only in English).
- Run on Skyrim AE 1.6.1170 by a player, as well as SE 1.5.97 and VR.
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
