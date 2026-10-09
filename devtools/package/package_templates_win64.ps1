# Packages built Windows export templates from bin/ into a .tpz in dist/
# (install it in the editor: Editor > Manage Export Templates > Install from File).
#
# Contents: the release and debug templates with their console wrappers, the
# D3D12 Agility SDK DLLs (named per architecture, as the Windows export
# expects) and the license files. PhysX GPU, Blast, Flow and, when installed,
# NVIDIA DLSS are not in the templates: the editor copies them from its own
# folder into every exported game (editor/export/native_runtime_export_plugin.cpp).
# NVIDIA Streamline / DLSS runtime files are never part of a public download.
#
# Build the templates first (see "Export templates" in CUSTOM_BUILD.md).
#
# Usage (from the repo root):
#   powershell -File devtools/package/package_templates_win64.ps1          # standard
#   powershell -File devtools/package/package_templates_win64.ps1 -Mono    # .NET
param(
	[switch]$Mono
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\..\.."
$bin = Join-Path $root "bin"
$suffix = if ($Mono) { ".mono" } else { "" }

$templates = @{
	"windows_release_x86_64.exe" = "godot.windows.template_release.x86_64$suffix.exe"
	"windows_release_x86_64_console.exe" = "godot.windows.template_release.x86_64$suffix.console.exe"
	"windows_debug_x86_64.exe" = "godot.windows.template_debug.x86_64$suffix.exe"
	"windows_debug_x86_64_console.exe" = "godot.windows.template_debug.x86_64$suffix.console.exe"
}
foreach ($source in $templates.Values) {
	if (-not (Test-Path (Join-Path $bin $source))) { throw "Missing bin\$source; build the templates first." }
}

# The editor installs templates into a folder named after their version
# (major.minor.status[.mono]) and only uses templates of its own version.
$full = (& (Join-Path $bin $templates["windows_release_x86_64_console.exe"]) --headless --version 2>$null | Select-Object -Last 1).Trim()
$templateVersion = $full -replace '\.custom_build\..*$', ''
if ($templateVersion.Split('.').Count -lt 3) { throw "Unexpected template version '$full'." }

$custom = (Get-Content (Join-Path $root "CUSTOM_VERSION") -TotalCount 1).Trim()
if ($custom -notmatch '^\d+\.\d+\.\d+$') { throw "CUSTOM_VERSION must be MAJOR.MINOR.PATCH, got '$custom'." }
$versionPy = Get-Content (Join-Path $root "version.py") -Raw
$godotMajor = [regex]::Match($versionPy, '(?m)^major = (\d+)').Groups[1].Value
$godotMinor = [regex]::Match($versionPy, '(?m)^minor = (\d+)').Groups[1].Value
$flavorSuffix = if ($Mono) { "_mono" } else { "" }
$name = "godot_$godotMajor.$godotMinor-nvidia-rt-dlss-physx_v$custom-export_templates_windows_amd64$flavorSuffix"

$dist = Join-Path $root "dist"
$stage = Join-Path $dist $name
$content = Join-Path $stage "templates"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $content | Out-Null

foreach ($target in $templates.Keys) {
	Copy-Item (Join-Path $bin $templates[$target]) (Join-Path $content $target)
}
foreach ($dll in @("D3D12Core", "d3d12SDKLayers")) {
	$source = Join-Path $bin "$dll.dll"
	if (-not (Test-Path $source)) { throw "Missing $source (D3D12 Agility SDK; it comes with a D3D12 build)." }
	Copy-Item $source (Join-Path $content "$dll.x86_64.dll")
}
Set-Content -Path (Join-Path $content "version.txt") -Value $templateVersion -NoNewline -Encoding ascii

$licenses = Join-Path $content "licenses"
New-Item -ItemType Directory -Force $licenses | Out-Null
Copy-Item (Join-Path $root "LICENSE.txt") (Join-Path $licenses "GODOT-LICENSE.txt")
Copy-Item (Join-Path $root "COPYRIGHT.txt") (Join-Path $licenses "GODOT-COPYRIGHT.txt")
Copy-Item (Join-Path $root "THIRD_PARTY_LICENSES.md") $licenses
Copy-Item (Join-Path $root "modules\godot_physx\PHYSX-LICENSE.md") $licenses
Copy-Item (Join-Path $root "thirdparty\streamline\LICENSE.txt") (Join-Path $licenses "NVIDIA-STREAMLINE-SDK-HEADERS-LICENSE.txt")
Get-ChildItem (Join-Path $root "misc\dist\licenses") -File | ForEach-Object { Copy-Item $_.FullName $licenses }

# A .tpz is a zip with a templates/ folder. Written with Python: Windows
# PowerShell's Compress-Archive can store backslashes in the entry names.
$tpz = Join-Path $dist "$name.tpz"
if (Test-Path $tpz) { Remove-Item -Force $tpz }
$py = "import os, sys, zipfile`nstage, out = sys.argv[1], sys.argv[2]`nwith zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:`n    for d, _, files in os.walk(os.path.join(stage, 'templates')):`n        for f in files:`n            p = os.path.join(d, f)`n            z.write(p, os.path.relpath(p, stage).replace(os.sep, '/'))`n"
python -c $py $stage $tpz
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $tpz)) { throw "Writing $tpz failed." }
Remove-Item -Recurse -Force $stage

Write-Host "Template version: $templateVersion"
Get-Item $tpz | Select-Object Name, @{ Name = "MB"; Expression = { [math]::Round($_.Length / 1MB, 1) } }
