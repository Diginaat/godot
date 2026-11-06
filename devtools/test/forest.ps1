param(
	[ValidateSet("vulkan", "d3d12")]
	[string]$Driver = "vulkan"
)
. "$PSScriptRoot\..\common.ps1"
$editor = Get-Editor
$project = Join-Path $RepoRoot "misc\large_world_test_project"
$logs = Join-Path $Out "forest"
New-Item -ItemType Directory -Force $logs | Out-Null
$ErrorActionPreference = "Continue"
$lines = & $editor --path $project --rendering-driver $Driver --quit-after 1200 res://forest.tscn 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$lines | Set-Content (Join-Path $logs "forest_$Driver.log")
$errors = @($lines | Where-Object { $_ -match "ERROR:|SCRIPT ERROR:|TEST_FAIL|leaked|ObjectDB instances" })
if ($code -ne 0 -or $errors.Count -gt 0 -or -not ($lines -match "^FOREST_TEST_PASS:")) {
	$lines | Write-Output
	Write-Result "Dense forest ($Driver)" $false "exit $code; see devtools/out/forest"
	exit 1
}
$lines | Where-Object { $_ -match "^FOREST_TEST_PASS:" } | Write-Output
Write-Result "Dense forest ($Driver)" $true "two-view lit/shadowed A/B screenshots"
