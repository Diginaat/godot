param(
	[ValidateSet("vulkan", "d3d12")]
	[string]$Driver = "vulkan",
	# rendering/limits/cluster_builder/max_clustered_elements per run (startup only).
	[string[]]$Limits = @("512", "4096"),
	[string]$Label = "run"
)
# Many omni lights over a field of boxes: GPU frame, render CPU and opaque
# pass time as the light count grows (0 to 4096), once per cluster limit.
# The limit goes into the project's override.cfg, removed afterwards.
. "$PSScriptRoot\..\common.ps1"
$editor = Get-Editor
$project = Join-Path $RepoRoot "misc\light_bench_project"
$logs = Join-Path $Out "light_bench"
New-Item -ItemType Directory -Force $logs | Out-Null
$ErrorActionPreference = "Continue"
# powershell -File passes "a,b" as one string.
$Limits = @($Limits | ForEach-Object { "$_" -split "," } | Where-Object { $_ } | ForEach-Object { [int]$_ })
$override = Join-Path $project "override.cfg"
$failed = $false
foreach ($limit in $Limits) {
	Set-Content -Path $override -Value "[rendering]`nlimits/cluster_builder/max_clustered_elements=$limit`n" -Encoding ascii
	$lines = & $editor --path $project --rendering-driver $Driver 2>&1 | ForEach-Object { "$_" }
	$code = $LASTEXITCODE
	$lines | Set-Content (Join-Path $logs "light_bench_${Label}_${Driver}_$limit.log")
	$lines | Where-Object { $_ -match "^LIGHT_BENCH(_SCENE)?:" } | Write-Output
	if ($code -ne 0 -or @($lines | Where-Object { $_ -match "^LIGHT_BENCH:" }).Count -ne 6 -or @($lines | Where-Object { $_ -match "ERROR:|SCRIPT ERROR:" }).Count -gt 0) {
		$failed = $true
	}
}
Remove-Item $override -ErrorAction SilentlyContinue
Write-Result "Light benchmark ($Driver)" (-not $failed) "see devtools/out/light_bench"
if ($failed) { exit 1 }
