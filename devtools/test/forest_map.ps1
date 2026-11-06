param(
	[ValidateSet("vulkan", "d3d12")]
	[string]$Driver = "vulkan",
	[ValidateSet("day", "night")]
	[string]$Variant = "day"
)
. "$PSScriptRoot\..\common.ps1"
$editor = Get-Editor
$project = Join-Path $RepoRoot "misc\large_world_test_project"
$scene = if ($Variant -eq "night") { "res://forest_night.tscn" } else { "res://forest_map.tscn" }
$logs = Join-Path $Out "forest_map"
New-Item -ItemType Directory -Force $logs | Out-Null
$ErrorActionPreference = "Continue"
$lines = & $editor --path $project --rendering-driver $Driver --quit-after 1200 $scene -- --forest-map-smoke 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$lines | Set-Content (Join-Path $logs "forest_map_${Variant}_$Driver.log")
$errors = @($lines | Where-Object { $_ -match "ERROR:|SCRIPT ERROR:|TEST_FAIL|leaked|ObjectDB instances" })
if ($code -ne 0 -or $errors.Count -gt 0 -or -not ($lines -match "^FOREST_MAP_TEST_PASS:")) {
	$lines | Where-Object { $_ -notmatch "signed by NVIDIA" } | Write-Output
	Write-Result "Valley fly-through $Variant ($Driver)" $false "exit $code; see devtools/out/forest_map"
	exit 1
}
$lines | Where-Object { $_ -match "^FOREST_MAP_(BUILD|TIMING|TEST_PASS):" } | Write-Output
Write-Result "Valley fly-through $Variant ($Driver)" $true "eight route screenshots in devtools/out/forest_map"
