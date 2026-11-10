param(
	[ValidateSet("vulkan", "d3d12")]
	[string]$Driver = "vulkan"
)
# GPU path for eligible instances: while the fixture moves, hides, re-layers,
# changes, gives meshes index LODs and shadow meshes, frees and adds nodes
# (also every frame, where buckets must not be rebuilt), adds an occluder
# and casters behind the camera,
# turns on TAA and FSR 2 (a motion pass) and opens more views, the engine compares
# what the GPU path drew with what the CPU cull found visible, and the
# fixture compares each phase's image (with TAA and FSR 2, the motion vectors) with
# and without the GPU path. Then a self-test run, where one draw bucket is
# left out, must make both checks fail.
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
$errors += @($lines | Where-Object { $_ -match "SCRIPT ERROR:|leaked|ObjectDB instances|GPU_INSTANCES_AB: differ|GPU_INSTANCES_MV: differ" })
# Index LODs chosen by the GPU path beyond level 0 (same counts as the CPU choice).
$lods = @($checks | Where-Object { $_ -match "lods=.*,[1-9][0-9]*:[1-9]" })
$shadows = @($checks | Where-Object { $_ -match "shadow_meshes=[1-9]" })
$moved = @($checks | Where-Object { $_ -match "moved=[1-9]" })
$vectors = @($lines | Where-Object { $_ -match "^GPU_INSTANCES_MV: equal" })
# Render info (objects, primitives) of GPU-drawn instances equal to the CPU lists' counts.
$info = @($lines | Where-Object { $_ -match "^GPU_INSTANCES_INFO: ok .*objects=[1-9]" })
# While nodes churn, their surfaces join and leave buckets without a rebuild.
$start = [array]::IndexOf($lines, "GPU_INSTANCES_PHASE: churn start")
$end = [array]::IndexOf($lines, "GPU_INSTANCES_PHASE: churn, 20 nodes replaced per frame")
$churn = @()
if ($start -ge 0 -and $end -gt $start) {
	$churn = @($lines[$start..$end] | Where-Object { $_ -match "^GPU_INSTANCES_CHECK: ok" } | ForEach-Object { if ($_ -match "rebuilds=(\d+)") { $Matches[1] } })
}
$incremental = $churn.Count -ge 2 -and @($churn | Select-Object -Unique).Count -eq 1
# The occluder must hide objects (occlusion culling took effect).
$occluded = @($lines | Where-Object { $_ -match "^GPU_INSTANCES_PHASE: occluder, objects (\d+) before, (\d+) with it" -and [int]$Matches[2] -lt [int]$Matches[1] })
# Shadow passes: what they drew equals what the scene cull listed, and
# casters outside the camera's view cast into it.
$shadowChecks = @($lines | Where-Object { $_ -match "^GPU_INSTANCES_SHADOW_CHECK: ok .*drawn=[1-9]" })
$casters = @($lines | Where-Object { $_ -match "^GPU_INSTANCES_CASTERS: ([1-9]\d*) pixels darker" })
# Shadow render info of GPU-drawn casters equal to the CPU path's, in the
# comparisons where both frames drew shadows (cached far cascades can skip
# a frame's shadow passes on either path).
$shadowPairs = @($lines | Where-Object { $_ -match "^GPU_INSTANCES_SHADOW_INFO: .* gpu=(\d+) cpu=(\d+)" -and [int]$Matches[1] -gt 0 -and [int]$Matches[2] -gt 0 } | ForEach-Object { $null = $_ -match "gpu=(\d+) cpu=(\d+)"; [pscustomobject]@{ Gpu = [int]$Matches[1]; Cpu = [int]$Matches[2] } })
$shadowInfo = @($shadowPairs | Where-Object { $_.Gpu -eq $_.Cpu })
$done = @($lines | Where-Object { $_ -match "^GPU_INSTANCES_TEST_DONE: .* 0 image differences" })
# Twelve phases with an image comparison each, two count checks per phase.
if ($code -ne 0 -or $errors.Count -gt 0 -or $done.Count -ne 1 -or $equal.Count -ne 12 -or $checks.Count -lt 24 -or $shadowChecks.Count -lt 10 -or $shadowInfo.Count -lt 6 -or $shadowInfo.Count -ne $shadowPairs.Count -or $casters.Count -ne 1 -or -not $incremental -or $occluded.Count -ne 1 -or $lods.Count -eq 0 -or $shadows.Count -eq 0 -or $moved.Count -eq 0 -or $vectors.Count -ne 2 -or $info.Count -lt 8) {
	$lines | Where-Object { $_ -match "ERROR|GPU_INSTANCES" } | Write-Output
	Write-Result "GPU instances ($Driver)" $false "exit $code, $($checks.Count) count checks, $($equal.Count) of 12 images equal, $($shadowChecks.Count) shadow checks, $($shadowInfo.Count) shadow render info matches, offscreen casters cast $($casters.Count) times, occluder hid objects $($occluded.Count) times, churn rebuilds $($churn -join '/'), $($lods.Count) with index LODs, $($shadows.Count) with shadow meshes, $($moved.Count) with a motion pass, $($vectors.Count) motion vector comparisons equal, $($info.Count) render info checks; see devtools/out/large_world"
	exit 1
}
Write-Result "GPU instances ($Driver)" $true "$($checks.Count) count checks ($($shadowChecks.Count) shadow checks, $($lods.Count) with index LODs, $($shadows.Count) with shadow meshes, $($moved.Count) with a motion pass, $($info.Count) render info checks), 12 of 12 images and the TAA and FSR 2 motion vectors equal to the CPU path, no rebuild while $($churn.Count) checks ran during churn: moves, layers, hidden, ineligible and back, index LODs and shadow meshes, free and add, churn, occluder, spot and omni shadows, casters behind the camera, TAA, FSR 2 motion pass, more views"

$lines = & $editor --path $project --rendering-driver $Driver --quit-after 1000 res://gpu_instances.tscn -- --self-test 2>&1 | ForEach-Object { "$_" }
$lines | Set-Content (Join-Path $logs "gpu_instances_selftest_$Driver.log")
$caught = @($lines | Where-Object { $_ -match "GPU instances check .*the GPU path drew" })
$image = @($lines | Where-Object { $_ -match "GPU_INSTANCES_AB: differ" })
if ($caught.Count -eq 0 -or $image.Count -eq 0) {
	Write-Result "GPU instances self-test ($Driver)" $false "count check reported $($caught.Count), image comparison $($image.Count) times"
	exit 1
}
Write-Result "GPU instances self-test ($Driver)" $true "missing bucket reported by $($caught.Count) count checks and the image comparison"
