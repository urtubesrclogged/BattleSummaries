# Third-party notices

Battle Summaries' own code is MIT-licensed and its SkyrimNet content is under the SkyrimNet Plugin License (see
LICENSE and LICENSE-CONTENT.md). It builds on the following work by others.

## Included in this repository

| File | From | License / terms |
|---|---|---|
| `src/papyrus/headers/*.psc` | Compile-time declarations of scripts from Skyrim (Actor, Form, Game, ...) and SkyrimNet (SkyrimNetApi) | Function and type signatures, there so this mod's scripts compile. No implementation is included; each script belongs to its own author (Bethesda Softworks; the SkyrimNet developer). Not covered by this mod's licenses. |

## Linked into BattleSummaries.dll, or used at build time

| Project | Used for | License |
|---|---|---|
| [CommonLibVR / CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR) (alandtse, ng branch) | SKSE plugin framework | MIT |
| [spdlog](https://github.com/gabime/spdlog) | logging | MIT |
| [fmt](https://github.com/fmtlib/fmt) | formatting | MIT |
| [nlohmann/json](https://github.com/nlohmann/json) | the summary handed to SkyrimNet | MIT |
| [SimpleIni](https://github.com/brofield/simpleini) | BattleSummaries.ini | MIT |
| [xbyak](https://github.com/herumi/xbyak), [rapidcsv](https://github.com/d99kris/rapidcsv), [toml11](https://github.com/ToruNiina/toml11), [DirectXMath](https://github.com/microsoft/DirectXMath), [DirectXTK](https://github.com/microsoft/DirectXTK) | CommonLib's own dependencies (vcpkg) | BSD-3-Clause / MIT |
| [Caprica](https://github.com/Orvid/Caprica) | Papyrus compiler (build only) | MIT |

## Runtime requirements (installed by the player, not redistributed)

SKSE / SKSE VR, Address Library for SKSE Plugins (VR: VR Address Library for SKSEVR) and SkyrimNet. Each is the work
of its own authors and under its own terms.

`TESV_Papyrus_Flags.flg` is Bethesda's file from the Creation Kit; it is not included - point `PAPYRUS_FLAGS` in
`local.env` at your own copy.

The Elder Scrolls V: Skyrim is (c) Bethesda Softworks / ZeniMax Media. This is an unofficial fan modification.
SkyrimNet is the work of its developer; Battle Summaries is an independent add-on and not part of SkyrimNet.
