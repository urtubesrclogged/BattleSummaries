#!/usr/bin/env bash
# Deploys Battle Summaries' build outputs into its mod-manager mod folders (NOT MO2's overwrite/, which would shadow
# them). MO2 locks its files while the game runs, so this refuses to deploy then.
set -e
R="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[ -f "$R/local.env" ] || { echo "local.env not found: copy local.env.example to local.env and set your paths" >&2; exit 1; }
while IFS= read -r line || [ -n "$line" ]; do   # KEY=VALUE lines (values may contain spaces); # comments skipped
	case "$line" in '#'*|'') continue ;; esac
	export "${line%%=*}=${line#*=}"
done < "$R/local.env"
# Targets: MOD_DIR and every MOD_DIR_<NAME> in local.env (one per modlist you test in). BSM_MOD_DIR (environment)
# replaces them all for one run, e.g. to stage a test build outside the mod manager.
TARGETS=()
if [ -n "$BSM_MOD_DIR" ]; then
	TARGETS=("$BSM_MOD_DIR")
else
	while IFS= read -r v; do TARGETS+=("${!v}"); done < <(compgen -v | grep -E '^MOD_DIR(_[A-Z0-9_]+)?$' | sort)
fi
[ ${#TARGETS[@]} -gt 0 ] || { echo "local.env: MOD_DIR is not set" >&2; exit 1; }
# Any running Skyrim locks its mod manager's files: GAME_EXE from local.env and both stock names are checked.
for exe in "${GAME_EXE:-SkyrimSE.exe}" SkyrimSE.exe SkyrimVR.exe; do
	if tasklist 2>/dev/null | grep -qi "$exe"; then
		echo "$exe is running - close the game before deploying." >&2
		exit 1
	fi
done
ID=urtubesrclogged.battle-summaries
OLD_ID=urtubesrclogged.battlesummaries   # the layer's id before 1.0.2
for M in "${TARGETS[@]}"; do
	[ -d "$(dirname "$M")" ] || { echo "skipped (no such mods folder): $M" >&2; continue; }
	mkdir -p "$M/SKSE/Plugins" "$M/Scripts" "$M/Source/Scripts"
	cp "$R/src/plugin/build/BattleSummaries.dll"        "$M/SKSE/Plugins/BattleSummaries.dll"
	cp "$R/config/SKSE/Plugins/BattleSummaries.ini"     "$M/SKSE/Plugins/BattleSummaries.ini"
	cp "$R"/build/papyrus/BattleSummaries_*.pex         "$M/Scripts/"
	cp "$R"/src/papyrus/BattleSummaries_*.psc           "$M/Source/Scripts/"
	# SkyrimNet 0.25+ content: an EXTERNAL layer, registered by SkyrimNet at start-up (docs/modding/CONTENT_ROOTS.md).
	SN="$M/SKSE/Plugins/SkyrimNet/external"
	rm -rf "$SN/$ID" "$SN/$OLD_ID" && mkdir -p "$SN" && cp -r "$R/config/SKSE/Plugins/SkyrimNet/external/$ID" "$SN/"
	# the settings shown on SkyrimNet's settings page (only the schema: the player's own settings.yaml is SkyrimNet's to write)
	mkdir -p "$M/SKSE/Plugins/SkyrimNet/config/plugins/BattleSummaries" && cp "$R/config/SKSE/Plugins/SkyrimNet/config/plugins/BattleSummaries/manifest.yaml" "$M/SKSE/Plugins/SkyrimNet/config/plugins/BattleSummaries/manifest.yaml"
	[ -f "$M/meta.ini" ] || printf '[General]\nmodid=0\nnotes=Battle Summaries for SkyrimNet. Needs SkyrimNet beta 25+ and Address Library.\n' > "$M/meta.ini"   # MO2 bookkeeping only
	echo "deployed to $M ($(find "$M" -type f | wc -l) files, DLL $(md5sum < "$M/SKSE/Plugins/BattleSummaries.dll" | cut -c1-8))"
done
