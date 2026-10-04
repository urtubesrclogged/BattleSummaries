#!/usr/bin/env bash
# Zips the source of Battle Summaries into release/Battle Summaries <version> - Source.zip, and unpacks the same
# tree in release/source/. An explicit include list (not "everything minus ignores"), so nothing new slips in by
# accident, followed by a privacy gate: the export is refused if any file in it names the author's machine or
# personal details. Run it before every push: what it refuses does not belong in the repository either.
#   bash package-source.sh     (version from project(VERSION) in src/plugin/CMakeLists.txt; or pass one)
set -e
R="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
V="${1:-$(sed -n 's/^project(BattleSummaries VERSION \([0-9.]*\).*/\1/p' "$R/src/plugin/CMakeLists.txt")}"
mkdir -p "$R/release"
if [ -f "$R/local.env" ]; then
	while IFS= read -r line || [ -n "$line" ]; do   # KEY=VALUE lines; # comments skipped
		case "$line" in '#'*|'') continue ;; esac
		export "${line%%=*}=${line#*=}"
	done < "$R/local.env"
fi
python - "$R" "$R/release/Battle Summaries $V - Source.zip" <<'PY'
import fnmatch, os, re, shutil, sys, zipfile
root, out = sys.argv[1], sys.argv[2]

INCLUDE = [
    "LICENSE", "LICENSE-CONTENT.md", "README.md", "CHANGELOG.md", "THIRD_PARTY_NOTICES.md", ".gitignore", ".gitattributes", "local.env.example",
    "build.ps1", "deploy.sh", "package.sh", "package-source.sh",
    "src/plugin/CMakeLists.txt", "src/plugin/vcpkg.json", "src/plugin/src/*", "src/plugin/tests/*",
    "src/papyrus/*.psc", "src/papyrus/headers/*.psc",
    "config/*",
]
EXCLUDE = ["*/__pycache__/*", "*.pyc", "local.env", "*/build/*"]
# The author's machine and identity: none of this may be published. User folders are always refused; add your own
# names, paths and handles as a regular expression in local.env (PRIVATE_PATTERNS=...), which is never published.
patterns = [r"C:[/\\]Users", r"/c/Users", r"/home/[a-z]"]
extra = os.environ.get("PRIVATE_PATTERNS", "").strip()
if extra:
    patterns.append(extra)
else:
    print("note: PRIVATE_PATTERNS is not set in local.env; only user-folder paths are checked")
PRIVATE = re.compile("|".join(patterns), re.I)

files = []
for dp, dns, fs in os.walk(root):
    rel_dir = os.path.relpath(dp, root).replace("\\", "/")
    if rel_dir.split("/")[0] in ("build", "release", "snapshots", ".git"):
        continue
    for f in fs:
        rel = f if rel_dir == "." else f"{rel_dir}/{f}"
        if any(fnmatch.fnmatch(rel, p) for p in INCLUDE) and not any(fnmatch.fnmatch(rel, p) for p in EXCLUDE):
            files.append(rel)
files.sort()

leaks = []
for rel in files:
    if rel == "package-source.sh":
        continue  # holds the patterns themselves
    try:
        text = open(os.path.join(root, rel), "rb").read().decode("utf-8")
    except UnicodeDecodeError:
        continue
    for n, line in enumerate(text.splitlines(), 1):
        if PRIVATE.search(line):
            leaks.append(f"{rel}:{n}: {line.strip()[:120]}")
if leaks:
    print("REFUSED - private details in the source:", *leaks, sep="\n  ")
    sys.exit(1)

with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for rel in files:
        z.write(os.path.join(root, rel), rel)
tree = os.path.join(root, "release", "source")
if os.path.isdir(tree):
    shutil.rmtree(tree)
for rel in files:
    dst = os.path.join(tree, rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(os.path.join(root, rel), dst)
print(f"{out}: {len(files)} files; unpacked in release/source/")
PY
