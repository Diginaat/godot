# Builds this fork with the options of this PC (custom.py, written by
# devtools\setup\setup_dev_windows.ps1), always with production=yes so the
# builds share their objects and stay incremental.
#
#   devtools\build\build.ps1                    # editor
#   devtools\build\build.ps1 -Mono              # C# (.NET) editor, glue and assemblies
#   devtools\build\build.ps1 -Target templates  # release and debug export templates
#   devtools\build\build.ps1 -Target templates -Mono
#   devtools\build\build.ps1 -Target release    # everything a GitHub release needs
#   ... -Jobs 10      parallel jobs (default: all cores)
#   ... -KeepGoing    scons -k: report all compile errors in one pass
#
# Run it from PowerShell or cmd, not Git Bash, and close any editor running
# from bin\ first (the link fails with "Access is denied").
param(
	[ValidateSet("editor", "templates", "release")]
	[string]$Target = "editor",
	[switch]$Mono,
	[int]$Jobs = 0,
	[switch]$KeepGoing
)
. "$PSScriptRoot\..\common.ps1"
Set-Location $RepoRoot

if (-not (Test-Path (Join-Path $RepoRoot "custom.py"))) {
	Write-Host "No custom.py: building without PhysX, Blast and Flow. Run devtools\setup\setup_dev_windows.ps1 to set them up." -ForegroundColor Yellow
}
if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }

function Invoke-Scons([string]$SconsTarget, [bool]$WithMono) {
	$sconsArgs = @("-m", "SCons", "-j$Jobs", "platform=windows", "target=$SconsTarget", "production=yes")
	if ($WithMono) { $sconsArgs += "module_mono_enabled=yes" }
	if ($KeepGoing) { $sconsArgs += "-k" }
	$label = "$SconsTarget$(if ($WithMono) { ' (.NET)' })"
	Write-Host "== Building $label" -ForegroundColor Cyan
	$start = Get-Date
	python @sconsArgs
	if ($LASTEXITCODE -ne 0) { throw "Build of $label failed." }
	Write-Host ("   done in {0:N1} min" -f ((Get-Date) - $start).TotalMinutes) -ForegroundColor Green
}

function Build-MonoAssemblies {
	Write-Host "== C# glue and assemblies" -ForegroundColor Cyan
	& (Join-Path $RepoRoot "bin\godot.windows.editor.x86_64.mono.console.exe") --headless --generate-mono-glue modules\mono\glue
	if ($LASTEXITCODE -ne 0) { throw "Generating the C# glue failed." }
	python modules/mono/build_scripts/build_assemblies.py --godot-output-dir=./bin --godot-platform=windows
	if ($LASTEXITCODE -ne 0) { throw "Building the C# assemblies failed." }
}

switch ($Target) {
	"editor" {
		Invoke-Scons "editor" $Mono
		if ($Mono) { Build-MonoAssemblies }
	}
	"templates" {
		Invoke-Scons "template_release" $Mono
		Invoke-Scons "template_debug" $Mono
	}
	"release" {
		# Commit (and bump CUSTOM_VERSION) first: the version stamp is the commit.
		foreach ($withMono in @($false, $true)) {
			Invoke-Scons "editor" $withMono
			Invoke-Scons "template_release" $withMono
			Invoke-Scons "template_debug" $withMono
		}
		Build-MonoAssemblies
	}
}
& (Get-Editor) --version
