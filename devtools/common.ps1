# Shared by the devtools scripts (dot-source it: . "$PSScriptRoot\..\common.ps1").
# Paths are derived from this file's location, never hard-coded.
$ErrorActionPreference = "Stop"
$DevRoot = (Resolve-Path "$PSScriptRoot").Path
$RepoRoot = (Resolve-Path "$DevRoot\..").Path
$Out = Join-Path $DevRoot "out"
New-Item -ItemType Directory -Force $Out | Out-Null

function Get-Editor([switch]$Mono) {
	$suffix = if ($Mono) { ".mono" } else { "" }
	$exe = Join-Path $RepoRoot "bin\godot.windows.editor.x86_64$suffix.console.exe"
	if (-not (Test-Path $exe)) { throw "Missing $exe. Build it first: devtools\build\build.ps1$(if ($Mono) { ' -Mono' })" }
	return $exe
}

# Lines that start with ERROR, minus the ones every build of this fork prints
# (AccessKit, the screen reader driver, is not part of the build).
function Get-RealErrors([string[]]$Lines) {
	return @($Lines | Where-Object { $_ -match "^ERROR" -and $_ -notmatch "screen reader support driver" })
}

function Write-Result([string]$Name, [bool]$Pass, [string]$Detail) {
	$mark = if ($Pass) { "PASS" } else { "FAIL" }
	$color = if ($Pass) { "Green" } else { "Red" }
	Write-Host ("{0}  {1,-34} {2}" -f $mark, $Name, $Detail) -ForegroundColor $color
}
