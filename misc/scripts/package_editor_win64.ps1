# Packages a built Windows editor from bin/ into a zip in dist/, with everything
# it needs at runtime: NVIDIA Streamline (DLSS, Reflex, NIS, DeepDVC, frame gen;
# the signed release DLLs, not bin/development), PhysX GPU, the D3D12 Agility
# SDK, licenses, and for .NET builds the GodotSharp folder.
#
# Usage (from the repo root):
#   powershell -File misc/scripts/package_editor_win64.ps1            # standard editor
#   powershell -File misc/scripts/package_editor_win64.ps1 -Mono      # .NET editor
param(
	[switch]$Mono
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\..\.."
$bin = Join-Path $root "bin"
$suffix = if ($Mono) { ".mono" } else { "" }
$exe = "godot.windows.editor.x86_64$suffix.exe"
$console = "godot.windows.editor.x86_64$suffix.console.exe"
if (-not (Test-Path (Join-Path $bin $exe))) { throw "Missing bin\$exe; build it first." }

$version = (& (Join-Path $bin $console) --version).Trim()
$flavor = if ($Mono) { "mono" } else { "standard" }
$name = "godot_v$($version)_nvidia-dlss-physx-gpu_editor_win64_$flavor"
$dist = Join-Path $root "dist"
$stage = Join-Path $dist $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null

$files = @($exe, $console, "PhysXGpu_64.dll", "D3D12Core.dll", "d3d12SDKLayers.dll") +
	(Get-ChildItem $bin -File -Filter "sl.*.dll" | ForEach-Object Name) +
	(Get-ChildItem $bin -File -Filter "nvngx_*.dll" | ForEach-Object Name) +
	@("NvLowLatencyVk.dll", "nis.license.txt", "nvngx_dlss.license.txt", "reflex.license.txt")
foreach ($file in $files) {
	$source = Join-Path $bin $file
	if (-not (Test-Path $source)) { throw "Missing bin\$file" }
	Copy-Item $source $stage
}
Copy-Item (Join-Path $root "LICENSE.txt") $stage
Copy-Item (Join-Path $root "COPYRIGHT.txt") $stage
Copy-Item (Join-Path $root "modules\godot_physx\PHYSX-LICENSE.md") $stage
if ($Mono) {
	$sharp = Join-Path $bin "GodotSharp"
	if (-not (Test-Path $sharp)) { throw "Missing bin\GodotSharp; run build_assemblies.py first." }
	Copy-Item -Recurse $sharp $stage
}

$zip = "$stage.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
& tar.exe -a -cf $zip -C $stage .
if ($LASTEXITCODE -ne 0) { throw "tar failed" }
Remove-Item -Recurse -Force $stage
Get-Item $zip | Select-Object Name, @{n = "MB"; e = { [math]::Round($_.Length / 1MB, 1) } }
