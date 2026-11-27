param(
	[ValidateSet("vulkan", "d3d12")]
	[string]$Driver = "vulkan",
	[string]$Label = "run"
)
# 1000 skinned characters posed every frame (misc/skin_bench_project): GPU
# frame, render CPU, frame setup CPU (scene instance updates and skeleton
# uploads) and the skinning compute time, with the whole crowd, a tenth of
# it and none of it in view.
. "$PSScriptRoot\..\common.ps1"
$editor = Get-Editor
$project = Join-Path $RepoRoot "misc\skin_bench_project"
$logs = Join-Path $Out "skin_bench"
New-Item -ItemType Directory -Force $logs | Out-Null
$ErrorActionPreference = "Continue"
$lines = & $editor --path $project --rendering-driver $Driver 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$lines | Set-Content (Join-Path $logs "skin_bench_${Label}_$Driver.log")
$lines | Where-Object { $_ -match "^SKIN_BENCH(_SCENE)?:" } | Write-Output
$failed = $code -ne 0 -or @($lines | Where-Object { $_ -match "^SKIN_BENCH:" }).Count -ne 3 -or @($lines | Where-Object { $_ -match "ERROR:|SCRIPT ERROR:" }).Count -gt 0
Write-Result "Skin benchmark ($Driver)" (-not $failed) "see devtools/out/skin_bench"
if ($failed) { exit 1 }
