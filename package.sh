#!/usr/bin/env bash
# Zips the built mod into release/Battle Summaries <version>.zip, ready to install in any mod manager (the archive root
# is the Data folder: SKSE/, Scripts/, Source/). Built from the build outputs and config/, not from a deployed folder.
#   bash package.sh            (version from project(VERSION) in src/plugin/CMakeLists.txt; or pass one)
# Run build.ps1 first. Refuses when the DLL is older than any source file, or when the layer manifest states another
# version than the DLL.
set -e
R="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
V="${1:-$(sed -n 's/^project(BattleSummaries VERSION \([0-9.]*\).*/\1/p' "$R/src/plugin/CMakeLists.txt")}"
mkdir -p "$R/release"
python - "$R" "$R/release/Battle Summaries $V.zip" "$V" <<'PY'
import glob, json, os, sys, zipfile
root, out, ver = sys.argv[1], sys.argv[2], sys.argv[3]
j = lambda *p: os.path.join(root, *p)
layer = "urtubesrclogged.battle-summaries"
dll = j("src", "plugin", "build", "BattleSummaries.dll")
if not os.path.exists(dll):
    sys.exit("no DLL: run build.ps1 first")
newest = max(os.path.getmtime(f) for f in glob.glob(j("src", "plugin", "src", "*")) + [j("src", "plugin", "CMakeLists.txt")])
if os.path.getmtime(dll) < newest:
    sys.exit("the DLL is older than its source: run build.ps1 first")
for psc in glob.glob(j("src", "papyrus", "BattleSummaries_*.psc")):
    pex = j("build", "papyrus", os.path.basename(psc)[:-4] + ".pex")
    if not os.path.exists(pex) or os.path.getmtime(pex) < os.path.getmtime(psc):
        sys.exit(f"{os.path.basename(pex)} is missing or older than its source: run build.ps1 first")
manifest = json.load(open(j("config", "SKSE", "Plugins", "SkyrimNet", "external", layer, "manifest.json"), encoding="utf-8"))
if manifest["version"] != ver:
    sys.exit(f"the layer manifest says {manifest['version']}, the DLL is {ver}: make them agree")

files = [(dll, "SKSE/Plugins/BattleSummaries.dll"), (j("config", "SKSE", "Plugins", "BattleSummaries.ini"), "SKSE/Plugins/BattleSummaries.ini")]
files += [(f, "Scripts/" + os.path.basename(f)) for f in sorted(glob.glob(j("build", "papyrus", "BattleSummaries_*.pex")))]
files += [(f, "Source/Scripts/" + os.path.basename(f)) for f in sorted(glob.glob(j("src", "papyrus", "BattleSummaries_*.psc")))]
base = j("config", "SKSE", "Plugins", "SkyrimNet", "external", layer)
for dp, _, fs in os.walk(base):
    for f in sorted(fs):
        full = os.path.join(dp, f)
        files.append((full, "SKSE/Plugins/SkyrimNet/external/" + layer + "/" + os.path.relpath(full, base).replace("\\", "/")))
# the licenses and credits travel with the mod (docs/ at the archive root; a mod manager leaves it out of Data or harmlessly in)
files += [(j("LICENSE"), "docs/Battle Summaries/LICENSE.txt"), (j("LICENSE-CONTENT.md"), "docs/Battle Summaries/LICENSE-CONTENT.md"),
          (j("THIRD_PARTY_NOTICES.md"), "docs/Battle Summaries/THIRD_PARTY_NOTICES.md"), (j("README.md"), "docs/Battle Summaries/README.md"),
          (j("CHANGELOG.md"), "docs/Battle Summaries/CHANGELOG.md")]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for src, arc in files:
        z.write(src, arc)
print(f"{out}: {len(files)} files, {os.path.getsize(out) / 1e6:.2f} MB")
for _, arc in files:
    print("  " + arc)
PY
