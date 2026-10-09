# The smoke tests of CUSTOM_BUILD.md, in one run. Every merge and every
# release needs them to pass.
#
#   devtools\test\smoke.ps1           # standard editor
#   devtools\test\smoke.ps1 -Mono     # .NET editor
#   ... -PhysXExample D:\godot-physx-example
#
# PhysX tests use Wild Ox Studios' example project
# (git clone https://github.com/uno1982/godot-physx-example next to this
# repository); they are skipped when it isn't there. The rendering tests use
# misc\ddgi_test_project. Logs go to devtools\out\smoke.
param(
	[switch]$Mono,
	[string]$PhysXExample = ""
)
. "$PSScriptRoot\..\common.ps1"
$editor = Get-Editor -Mono:$Mono
if (-not $PhysXExample) { $PhysXExample = Join-Path (Split-Path $RepoRoot -Parent) "godot-physx-example" }
$project = Join-Path $RepoRoot "misc\ddgi_test_project"
$logs = Join-Path $Out "smoke\$(if ($Mono) { 'mono' } else { 'standard' })"
New-Item -ItemType Directory -Force $logs | Out-Null
$failed = 0

function Run-Test([string]$Name, [string[]]$GodotArgs, [scriptblock]$Check) {
	# The engine writes warnings to stderr; Windows PowerShell would turn them
	# into terminating errors under "Stop". The output is checked below instead.
	$ErrorActionPreference = "Continue"
	$lines = & $editor @GodotArgs 2>&1 | ForEach-Object { "$_" }
	$code = $LASTEXITCODE
	$lines | Set-Content (Join-Path $logs (($Name -replace '[^\w]+', '_') + ".log"))
	$errors = Get-RealErrors $lines
	$detail = & $Check $lines
	$pass = $code -eq 0 -and $errors.Count -eq 0 -and -not ($detail -like "missing*")
	if (-not $pass -and $errors.Count -gt 0) { $detail = "$($errors.Count) errors, first: $($errors[0])" }
	if ($code -ne 0) { $detail = "exit code $code; $detail" }
	Write-Result $Name $pass $detail
	if (-not $pass) { $script:failed++ }
}

Write-Host "Editor: $(& $editor --version 2>$null)"

if (Test-Path (Join-Path $PhysXExample "project.godot")) {
	$physxCheck = { param($l) $m = $l | Where-Object { $_ -match "PhysX 5.*initialized" } | Select-Object -First 1; if ($m) { $m.Trim() } else { "missing 'PhysX ... initialized'" } }
	Run-Test "PhysX, game" @("--verbose", "--path", $PhysXExample, "--quit-after", "600") $physxCheck
	Run-Test "PhysX, editor" @("--editor", "--verbose", "--path", $PhysXExample, "--quit-after", "600") $physxCheck
} else {
	Write-Host "SKIP  PhysX tests: no example project at $PhysXExample" -ForegroundColor Yellow
}

$shot = Join-Path $logs "path_tracer.png"
Run-Test "Path tracer, Vulkan" @("--rendering-driver", "vulkan", "--path", $project, "--", "--view=room", "--gi=none", "--pt=1", "--res=640x360", "--frames=120", "--shot=$shot") {
	param($l)
	if ($l -match "Raytracing not supported") { "missing ray tracing ('Raytracing not supported')" }
	elseif (Test-Path $shot) { "screenshot $shot" } else { "missing screenshot" }
}
Run-Test "DDGI, interior" @("--path", $project, "--", "--view=interior", "--gi=ddgi", "--tod=0", "--anim=0", "--res=640x360", "--frames=300", "--measure=30") {
	param($l)
	$m = $l | Where-Object { $_ -match "^MEASURE" } | Select-Object -First 1
	if ($m) { ($m -replace '^MEASURE\s+', '') } else { "missing MEASURE line" }
}

if ($failed -gt 0) { Write-Host "$failed test(s) failed; logs in $logs" -ForegroundColor Red; exit 1 }
Write-Host "All smoke tests passed." -ForegroundColor Green
