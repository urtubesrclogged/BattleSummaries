# Builds Battle Summaries. Usage: powershell -File build.ps1 [-Only papyrus|dll|test] [-Configure]
#   papyrus : Caprica -> build/papyrus
#   dll     : CMake/Ninja -> src/plugin/build/BattleSummaries.dll, after the offline tests pass
#   test    : only builds and runs the offline tests (the battle model and its narrative; no game needed)
# Paths to your tools come from local.env (copy local.env.example).
param([string]$Only = "", [switch]$Configure)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

$cfg = @{}
$envFile = Join-Path $root "local.env"
if (-not (Test-Path $envFile)) { throw "local.env not found: copy local.env.example to local.env and set your paths" }
foreach ($line in Get-Content $envFile) {
  if ($line -match '^\s*([A-Z_]+)\s*=\s*(.*?)\s*$') { $cfg[$Matches[1]] = $Matches[2] }
}
function Need([string]$key) { if (-not $cfg[$key]) { throw "local.env: $key is not set" }; return $cfg[$key] }
function NeedWin([string]$key) { return (Need $key).Replace('/', '\') }  # cmd / vcvars want backslashes

if (-not $Only -or $Only -eq "papyrus") {
  New-Item -ItemType Directory -Force "$root\build\papyrus" | Out-Null
  Push-Location "$root\src\papyrus"
  & (Need "CAPRICA") --game skyrim --import headers --import . --flags (Need "PAPYRUS_FLAGS") --output "$root\build\papyrus" BattleSummaries_Native.psc BattleSummaries_SkyrimNet.psc
  $rc = $LASTEXITCODE
  Pop-Location
  if ($rc -ne 0) { throw "Caprica failed" }
}

if (-not $Only -or $Only -eq "dll" -or $Only -eq "test") {
  $src   = "$root\src\plugin"
  $vc    = NeedWin "VCVARS"
  $cm    = NeedWin "CMAKE"
  $ninja = NeedWin "NINJA"
  $commonlib = Need "COMMONLIB_DIR"
  $vcpkg = Need "VCPKG_ROOT"
  $env:PATH += ";C:\Program Files (x86)\Microsoft Visual Studio\Installer"
  if ($Configure -or -not (Test-Path "$src\build\build.ninja")) {
    cmd /c "`"$vc`" >nul 2>&1 && `"$cm`" -S $src -B $src\build -G Ninja -DCMAKE_MAKE_PROGRAM=`"$ninja`" -DCMAKE_BUILD_TYPE=Release -DCOMMONLIB_DIR=`"$commonlib`" -DCMAKE_TOOLCHAIN_FILE=`"$vcpkg/scripts/buildsystems/vcpkg.cmake`" -DVCPKG_TARGET_TRIPLET=x64-windows-static-md 2>&1"
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
  }
  # 2>&1 inside cmd: Windows PowerShell turns any native stderr line (vcvars sometimes prints one) into a terminating
  # error even when the build succeeds; success is decided by the exit code below
  cmd /c "`"$vc`" >nul 2>&1 && `"$cm`" --build $src\build --target BattleSummariesTests 2>&1"
  if ($LASTEXITCODE -ne 0) { throw "test build failed" }
  & "$src\build\BattleSummariesTests.exe"
  if ($LASTEXITCODE -ne 0) { throw "offline tests failed" }
  if ($Only -ne "test") {
    cmd /c "`"$vc`" >nul 2>&1 && `"$cm`" --build $src\build --target BattleSummaries 2>&1"
    if ($LASTEXITCODE -ne 0) { throw "DLL build failed" }
  }
}
