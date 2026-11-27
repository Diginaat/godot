param(
	[ValidateSet("vulkan", "d3d12")]
	[string]$Driver = "vulkan",
	[string]$Label = "run"
)
# Cluster LOD in shadow passes: a field of dense CLOD meshes under a
# directional light (4 cascades), a spot and an omni light (cube), shadow
# passes with source meshes against their own CLOD cut, alternating order.
# GPU frame time from the viewport, the shadow stage from RD timestamps.
. "$PSScriptRoot\..\common.ps1"
$editor = Get-Editor
$project = Join-Path $RepoRoot "misc\large_world_test_project"
$logs = Join-Path $Out "shadow_clod_bench"
New-Item -ItemType Directory -Force $logs | Out-Null
$ErrorActionPreference = "Continue"
$lines = & $editor --path $project --rendering-driver $Driver res://shadow_clod.tscn -- --bench 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$lines | Set-Content (Join-Path $logs "shadow_clod_bench_${Label}_$Driver.log")
$rows = @($lines | Where-Object { $_ -match "^SHADOW_CLOD_BENCH:" })
$lines | Where-Object { $_ -match "^SHADOW_CLOD_BENCH_SCENE:" } | Write-Output
$rows | Write-Output
# Two views, two modes.
if ($code -ne 0 -or $rows.Count -ne 4 -or @($lines | Where-Object { $_ -match "ERROR:|SCRIPT ERROR:|SHADOW_CLOD_TEST_FAIL" }).Count -gt 0) {
	Write-Result "Shadow CLOD benchmark ($Driver)" $false "exit $code; see devtools/out/shadow_clod_bench"
	exit 1
}
Write-Result "Shadow CLOD benchmark ($Driver)" $true "4 rows in devtools/out/shadow_clod_bench"
