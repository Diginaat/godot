# Packages a built Windows editor from bin/ into a zip in dist/.
#
# Default (public release): the editor, PhysX GPU, the D3D12 Agility SDK
# DLLs, all license files and a notice explaining where to get NVIDIA DLSS.
# NVIDIA Streamline / DLSS / Reflex runtime files are NOT included: their
# licenses don't allow redistributing them in a public open-source download
# (see THIRD_PARTY_LICENSES.md).
#
# -WithNvidiaRuntime: also bundles the Streamline release DLLs from bin/
# (never bin/development). For your own machines only; do not publish.
#
# Usage (from the repo root):
#   powershell -File misc/scripts/package_editor_win64.ps1                 # standard editor
#   powershell -File misc/scripts/package_editor_win64.ps1 -Mono           # .NET editor
#   powershell -File misc/scripts/package_editor_win64.ps1 -WithNvidiaRuntime
param(
	[switch]$Mono,
	[switch]$WithNvidiaRuntime
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
$variant = if ($WithNvidiaRuntime) { "_with-nvidia-runtime_PRIVATE" } else { "" }
$name = "godot_v$($version)_nvidia-pathtracing-physx-gpu_editor_win64_$flavor$variant"
$dist = Join-Path $root "dist"
$stage = Join-Path $dist $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null

function Copy-Required($source, $destination) {
	if (-not (Test-Path $source)) { throw "Missing $source" }
	Copy-Item $source $destination
}

foreach ($file in @($exe, $console, "PhysXGpu_64.dll", "D3D12Core.dll", "d3d12SDKLayers.dll")) {
	Copy-Required (Join-Path $bin $file) $stage
}

$licenses = Join-Path $stage "licenses"
New-Item -ItemType Directory -Force $licenses | Out-Null
Copy-Required (Join-Path $root "LICENSE.txt") (Join-Path $licenses "GODOT-LICENSE.txt")
Copy-Required (Join-Path $root "COPYRIGHT.txt") (Join-Path $licenses "GODOT-COPYRIGHT.txt")
Copy-Required (Join-Path $root "THIRD_PARTY_LICENSES.md") $licenses
Copy-Required (Join-Path $root "modules\godot_physx\PHYSX-LICENSE.md") $licenses
Copy-Required (Join-Path $root "thirdparty\streamline\LICENSE.txt") (Join-Path $licenses "NVIDIA-STREAMLINE-SDK-HEADERS-LICENSE.txt")
Get-ChildItem (Join-Path $root "misc\dist\licenses") -File | ForEach-Object { Copy-Item $_.FullName $licenses }

if ($WithNvidiaRuntime) {
	$nvidia = @(Get-ChildItem $bin -File -Filter "sl.*.dll" | Where-Object Name -ne "sl.nvperf.dll" | ForEach-Object Name) +
		(Get-ChildItem $bin -File -Filter "nvngx_*.dll" | ForEach-Object Name) +
		@("NvLowLatencyVk.dll", "nis.license.txt", "nvngx_dlss.license.txt", "reflex.license.txt")
	foreach ($file in $nvidia) {
		Copy-Required (Join-Path $bin $file) $stage
	}
} else {
	$notice = @"
NVIDIA DLSS IS NOT INCLUDED IN THIS DOWNLOAD
============================================

This editor supports NVIDIA DLSS Super Resolution, DLSS Ray Reconstruction,
DLSS Frame Generation and NVIDIA Reflex through the NVIDIA Streamline SDK.
NVIDIA's license terms don't allow those runtime files to be redistributed
in this download, so they are not included.

Without them the editor works normally (including the path tracer and PhysX),
only DLSS, Ray Reconstruction, Frame Generation and Reflex are unavailable.

To enable them:

1. Download NVIDIA Streamline SDK 2.10.0:
   https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.10.0
2. From the SDK's bin\x64 folder (NOT bin\x64\development), copy these
   files next to $exe:
     sl.interposer.dll and the other sl.*.dll files
     nvngx_dlss.dll, nvngx_dlssd.dll, nvngx_dlssg.dll, nvngx_deepdvc.dll
     NvLowLatencyVk.dll
     nvngx_dlss.license.txt, reflex.license.txt, nis.license.txt
3. Restart the editor.

By downloading those files you accept NVIDIA's license terms, which are
included in the SDK. Full instructions and all licenses:
https://github.com/Diginaat/godot/tree/nvidia-dlss-physx#readme
"@
	Set-Content -Path (Join-Path $stage "NVIDIA_DLSS_NOT_INCLUDED_README.txt") -Value $notice -Encoding utf8
}

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
