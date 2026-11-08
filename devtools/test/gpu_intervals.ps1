param(
	[ValidateSet("vulkan", "d3d12")]
	[string]$Driver = "vulkan",
	[string]$Scene = "res://forest_map.tscn",
	[string]$Label = "run",
	# Extra user arguments for the scene, for example --far-static.
	[string[]]$ExtraArgs = @()
)
# Whole-frame GPU time per timestamp interval (misc/large_world_test_project/gpu_intervals.gd).
# Covers work outside the viewport's render time, which the benchmarks read.
. "$PSScriptRoot\..\common.ps1"
$editor = Get-Editor
$project = Join-Path $RepoRoot "misc\large_world_test_project"
$logs = Join-Path $Out "gpu_intervals"
New-Item -ItemType Directory -Force $logs | Out-Null
$ErrorActionPreference = "Continue"
$userArgs = [string[]](@("--prof-scene=$Scene") + $ExtraArgs)
$lines = & $editor --path $project --rendering-driver $Driver --gpu-profile --quit-after 3000 res://gpu_intervals.tscn -- @userArgs 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$lines | Set-Content (Join-Path $logs "intervals_${Label}_$Driver.log")
$lines | Where-Object { $_ -match "^GPU_INTERVAL" } | Write-Output
$total = @($lines | Where-Object { $_ -match "^GPU_INTERVAL_TOTAL:" })
if ($code -ne 0 -or $total.Count -ne 1 -or @(Get-RealErrors $lines).Count -gt 0 -or @($lines | Where-Object { $_ -match "SCRIPT ERROR:" }).Count -gt 0) {
	Write-Result "GPU intervals ($Driver)" $false "exit $code; see devtools/out/gpu_intervals"
	exit 1
}
Write-Result "GPU intervals ($Driver)" $true $total[0]
