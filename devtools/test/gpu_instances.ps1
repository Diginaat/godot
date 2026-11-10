param(
	[ValidateSet("vulkan", "d3d12")]
	[string]$Driver = "vulkan"
)
# GPU path for eligible instances: while the fixture moves, hides, re-layers,
# changes, gives meshes index LODs and shadow meshes, frees and adds nodes
# and opens more views, the engine compares what the GPU path drew with what
# the CPU cull found visible, and the fixture compares each phase's image
# with and without the GPU path. Then a self-test
# run, where one draw bucket is left out, must make both checks fail.
. "$PSScriptRoot\..\common.ps1"
$editor = Get-Editor
$project = Join-Path $RepoRoot "misc\large_world_test_project"
$logs = Join-Path $Out "large_world"
New-Item -ItemType Directory -Force $logs | Out-Null
$ErrorActionPreference = "Continue"

$lines = & $editor --path $project --rendering-driver $Driver --quit-after 3000 res://gpu_instances.tscn 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$lines | Set-Content (Join-Path $logs "gpu_instances_$Driver.log")
# Counts must have compared real draws.
$checks = @($lines | Where-Object { $_ -match "^GPU_INSTANCES_CHECK: ok .*drawn=[1-9]" })
$equal = @($lines | Where-Object { $_ -match "^GPU_INSTANCES_AB: equal" })
$errors = Get-RealErrors $lines
$errors += @($lines | Where-Object { $_ -match "SCRIPT ERROR:|leaked|ObjectDB instances|GPU_INSTANCES_AB: differ" })
# Index LODs chosen by the GPU path beyond level 0 (same counts as the CPU choice).
$lods = @($checks | Where-Object { $_ -match "lods=.*,[1-9][0-9]*:[1-9]" })
$shadows = @($checks | Where-Object { $_ -match "shadow_meshes=[1-9]" })
$done = @($lines | Where-Object { $_ -match "^GPU_INSTANCES_TEST_DONE: .* 0 image differences" })
# Eight phases with an image comparison each, two count checks per phase.
if ($code -ne 0 -or $errors.Count -gt 0 -or $done.Count -ne 1 -or $equal.Count -ne 8 -or $checks.Count -lt 16 -or $lods.Count -eq 0 -or $shadows.Count -eq 0) {
	$lines | Where-Object { $_ -match "ERROR|GPU_INSTANCES" } | Write-Output
	Write-Result "GPU instances ($Driver)" $false "exit $code, $($checks.Count) count checks, $($equal.Count) of 8 images equal, $($lods.Count) with index LODs, $($shadows.Count) with shadow meshes; see devtools/out/large_world"
	exit 1
}
Write-Result "GPU instances ($Driver)" $true "$($checks.Count) count checks ($($lods.Count) with index LODs, $($shadows.Count) with shadow meshes), 8 of 8 images equal to the CPU path: moves, layers, hidden, ineligible and back, index LODs and shadow meshes, free and add, more views"

$lines = & $editor --path $project --rendering-driver $Driver --quit-after 1000 res://gpu_instances.tscn -- --self-test 2>&1 | ForEach-Object { "$_" }
$lines | Set-Content (Join-Path $logs "gpu_instances_selftest_$Driver.log")
$caught = @($lines | Where-Object { $_ -match "GPU instances check .*the GPU path drew" })
$image = @($lines | Where-Object { $_ -match "GPU_INSTANCES_AB: differ" })
if ($caught.Count -eq 0 -or $image.Count -eq 0) {
	Write-Result "GPU instances self-test ($Driver)" $false "count check reported $($caught.Count), image comparison $($image.Count) times"
	exit 1
}
Write-Result "GPU instances self-test ($Driver)" $true "missing bucket reported by $($caught.Count) count checks and the image comparison"
