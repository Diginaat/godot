param(
	[ValidateSet("vulkan", "d3d12")]
	[string]$Driver = "vulkan",
	[string]$Label = "run",
	# ab: every mode also with one indirect record per CLOD group (compact_commands off).
	[ValidateSet("compact", "ab")]
	[string]$Commands = "compact"
)
# CLOD HZB occlusion benchmark: street grid, three views, occlusion off /
# single pass / two passes / two passes + MultiMesh occlusion, alternating order. GPU times from the viewport
# and RD timestamps (depth pre-pass, HZB, phase 2, opaque pass).
. "$PSScriptRoot\..\common.ps1"
$editor = Get-Editor
$project = Join-Path $RepoRoot "misc\large_world_test_project"
$logs = Join-Path $Out "hzb_bench"
New-Item -ItemType Directory -Force $logs | Out-Null
$ErrorActionPreference = "Continue"
$lines = & $editor --path $project --rendering-driver $Driver res://hzb_bench.tscn -- "--commands=$Commands" 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$lines | Set-Content (Join-Path $logs "hzb_bench_${Label}_$Driver.log")
$rows = @($lines | Where-Object { $_ -match "^HZB_BENCH:" })
$lines | Where-Object { $_ -match "^HZB_BENCH_(SCENE|STATS):" } | Write-Output
$rows | Write-Output
# Three views, five modes (twice with -Commands ab).
$expected = if ($Commands -eq "ab") { 30 } else { 15 }
if ($code -ne 0 -or $rows.Count -ne $expected -or @($lines | Where-Object { $_ -match "ERROR:|SCRIPT ERROR:|HZB_BENCH_FAIL" }).Count -gt 0) {
	Write-Result "HZB benchmark ($Driver)" $false "exit $code; see devtools/out/hzb_bench"
	exit 1
}
Write-Result "HZB benchmark ($Driver)" $true "$expected rows in devtools/out/hzb_bench"
