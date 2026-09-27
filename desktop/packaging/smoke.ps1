# Starts the packaged Windows app from its own files (never the runner's Qt or PATH) and fails
# on a DLL, plugin or runtime the deploy left out. Needs dumpbin (the MSVC developer shell).
#   pwsh desktop/packaging/smoke.ps1 <package.zip>
param([Parameter(Mandatory)][string]$Package)
$ErrorActionPreference = 'Stop'

$work = Join-Path ([IO.Path]::GetTempPath()) "stencil-smoke-$PID"
Expand-Archive $Package $work -Force
$root = (Get-ChildItem $work -Directory | Select-Object -First 1).FullName
$bin = Join-Path $root 'bin'
$sys = Join-Path $env:SystemRoot 'System32'

foreach ($p in 'platforms\qwindows', 'imageformats\qjpeg', 'imageformats\qwebp', 'multimedia\ffmpegmediaplugin') {
  if (-not (Test-Path "$root\plugins\$p.dll")) { throw "smoke: the package carries no plugins\$p.dll" }
}
# The runner has the VC++ runtime system-wide, so only files in bin\ prove the package has it.
$crt = '^(vcruntime|msvcp|concrt)'
foreach ($f in 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll') {
  if (-not (Test-Path "$bin\$f") -and -not (Test-Path "$bin\vc_redist.x64.exe")) { throw "smoke: no VC++ runtime ($f)" }
}

# Every import of every shipped binary resolves in bin\ or the OS itself.
foreach ($b in Get-ChildItem $root -Recurse -Include *.exe, *.dll) {
  $deps = (dumpbin /nologo /dependents $b.FullName) -match '^\s+\S+\.dll\s*$' | ForEach-Object { $_.Trim() }
  foreach ($d in $deps) {
    if ($d -match '^(api|ext)-ms-' -or (Test-Path "$bin\$d")) { continue }
    if ($d -notmatch $crt -and (Test-Path "$sys\$d")) { continue }
    throw "smoke: $($b.Name) imports $d, which the package does not carry"
  }
}

# SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX, inherited by the child: a loader failure
# exits at once instead of waiting on a dialog. A healthy GUI app is still running 10 s later.
Add-Type -Namespace Smoke -Name Native -MemberDefinition '[DllImport("kernel32.dll")] public static extern uint SetErrorMode(uint mode);'
[void][Smoke.Native]::SetErrorMode(0x0003)
Remove-Item Env:QT_PLUGIN_PATH, Env:QML2_IMPORT_PATH -ErrorAction SilentlyContinue
$env:PATH = "$sys;$env:SystemRoot"
$app = Start-Process "$bin\stencil.exe" -PassThru
Start-Sleep -Seconds 10
if ($app.HasExited) { throw "smoke: stencil.exe exited early ($($app.ExitCode))" }
Stop-Process $app -Force
Write-Host "smoke: $(Split-Path $Package -Leaf) starts from its own files"
