param(
	[ValidateSet("vulkan", "d3d12")]
	[string]$Driver = "vulkan"
)
# GPU instance table: the engine compares the GPU buffer with its CPU copy
# while the fixture adds, moves, changes, frees and re-adds nodes. Then a
# self-test run, where one record is left out of each upload, must report a
# difference (proves the check can fail).
. "$PSScriptRoot\..\common.ps1"
$editor = Get-Editor
$project = Join-Path $RepoRoot "misc\large_world_test_project"
$logs = Join-Path $Out "large_world"
New-Item -ItemType Directory -Force $logs | Out-Null
$ErrorActionPreference = "Continue"

$lines = & $editor --path $project --rendering-driver $Driver --quit-after 2000 res://instance_table.tscn 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$lines | Set-Content (Join-Path $logs "instance_table_$Driver.log")
$checks = @($lines | Where-Object { $_ -match "^GPU_INSTANCE_TABLE_CHECK: ok" })
$errors = Get-RealErrors $lines
$errors += @($lines | Where-Object { $_ -match "SCRIPT ERROR:|leaked|ObjectDB instances" })
$done = @($lines | Where-Object { $_ -match "^INSTANCE_TABLE_TEST_DONE:" })
# Eight phases of 90 frames, one check per 30 frames: expect well over 12.
# Both upload paths must have run: direct ranges and the packed scatter.
$direct = @($checks | Where-Object { $_ -match "direct_uploads=[1-9]" }).Count
$scattered = @($checks | Where-Object { $_ -match "scattered_uploads=[1-9]" }).Count
if ($code -ne 0 -or $errors.Count -gt 0 -or $done.Count -ne 1 -or $checks.Count -lt 12 -or $direct -eq 0 -or $scattered -eq 0) {
	$lines | Where-Object { $_ -match "ERROR|INSTANCE_TABLE|GPU_INSTANCE_TABLE" } | Write-Output
	Write-Result "GPU instance table ($Driver)" $false "exit $code, $($checks.Count) checks; see devtools/out/large_world"
	exit 1
}
Write-Result "GPU instance table ($Driver)" $true "$($checks.Count) matching checks ($direct after direct, $scattered after scattered uploads): growth, moves, layers, materials, reuse, rebuild"

$lines = & $editor --path $project --rendering-driver $Driver --quit-after 1000 res://instance_table.tscn -- --self-test 2>&1 | ForEach-Object { "$_" }
$lines | Set-Content (Join-Path $logs "instance_table_selftest_$Driver.log")
$caught = @($lines | Where-Object { $_ -match "GPU instance table check .* differs from the CPU mirror" })
if ($caught.Count -eq 0) {
	Write-Result "GPU instance table self-test ($Driver)" $false "a dropped upload was not reported"
	exit 1
}
Write-Result "GPU instance table self-test ($Driver)" $true "dropped upload reported ($($caught.Count) times)"
