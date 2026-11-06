param(
	[ValidateSet("vulkan", "d3d12")]
	[string]$Driver = "vulkan",
	[string]$Label = "run",
	# Extra user arguments for the scene, for example --no-mesh-lods.
	[string[]]$ExtraArgs = @()
)
# Valley benchmark: three fixed views, four GPU-geometry modes, alternating order.
. "$PSScriptRoot\..\common.ps1"
$editor = Get-Editor
$project = Join-Path $RepoRoot "misc\large_world_test_project"
$logs = Join-Path $Out "forest_bench"
New-Item -ItemType Directory -Force $logs | Out-Null
$ErrorActionPreference = "Continue"
$lines = & $editor --path $project --rendering-driver $Driver res://forest_map.tscn -- --forest-map-bench @ExtraArgs 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$lines | Set-Content (Join-Path $logs "bench_${Label}_$Driver.log")
$rows = @($lines | Where-Object { $_ -match "^FOREST_BENCH:" })
$lines | Where-Object { $_ -match "^FOREST_BENCH_(PATCHES|STATS):" } | Write-Output
$rows | Write-Output
if ($code -ne 0 -or $rows.Count -ne 12 -or @($lines | Where-Object { $_ -match "ERROR:|SCRIPT ERROR:" }).Count -gt 0) {
	Write-Result "Valley benchmark ($Driver)" $false "exit $code; see devtools/out/forest_bench"
	exit 1
}
Write-Result "Valley benchmark ($Driver)" $true "12 rows in devtools/out/forest_bench"
