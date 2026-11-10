param(
	[ValidateSet("vulkan", "d3d12")]
	[string]$Driver = "vulkan",
	[ValidateSet("safe", "separate")]
	[string]$RenderThread = "safe",
	[string]$Label = "run",
	# Per-stage CPU times (RENDER_TIMESTAMP markers) instead of timing.
	[switch]$Stages,
	# Turn on rendering/geometry/gpu_instance_table.
	[switch]$Table,
	# Turn on rendering/geometry/gpu_instances (the GPU path for eligible instances).
	[switch]$GpuInstances
)
# Many-instance CPU benchmark (docs/renderer/benchmarks.md): separate
# MeshInstance3D nodes from 1k to 100k, shared and unique materials, moving
# instances, many lights, material changes every frame; static and orbiting views, two repeats. Prints wall
# frame time, render CPU and GPU p50/p95/p99, draw counts and per-stage CPU.
. "$PSScriptRoot\..\common.ps1"
$editor = Get-Editor
$project = Join-Path $RepoRoot "misc\large_world_test_project"
$logs = Join-Path $Out "instance_bench"
New-Item -ItemType Directory -Force $logs | Out-Null
$ErrorActionPreference = "Continue"
# --quit-after is a hard stop: a script that fails to load leaves an empty
# scene running forever.
$mode = if ($Stages) { @("--gpu-profile", "--quit-after", "10000", "res://instance_bench.tscn") } else { @("--quit-after", "30000", "res://instance_bench.tscn") }
$userArgs = @()
if ($Stages) { $userArgs += "--stages" }
if ($Table) { $userArgs += "--table" }
if ($GpuInstances) { $userArgs += "--gpu-instances" }
if ($userArgs.Count -gt 0) { $mode += @("--") + $userArgs }
$lines = & $editor --path $project --rendering-driver $Driver --render-thread $RenderThread @mode 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$kind = $(if ($Stages) { "stages" } else { "timing" }) + $(if ($Table) { "_table" } else { "" }) + $(if ($GpuInstances) { "_gpu" } else { "" })
$lines | Set-Content (Join-Path $logs "instance_bench_${Label}_${Driver}_${RenderThread}_$kind.log")
$rows = @($lines | Where-Object { $_ -match $(if ($Stages) { "^INSTANCE_BENCH_STAGES:" } else { "^INSTANCE_BENCH:" }) })
$expected = if ($Stages) { 14 } else { 28 }
$lines | Where-Object { $_ -match "^INSTANCE_BENCH_(SCENE|BUILD):" } | Write-Output
$rows | Write-Output
if ($code -ne 0 -or $rows.Count -ne $expected -or @($lines | Where-Object { $_ -match "ERROR:|SCRIPT ERROR:|INSTANCE_BENCH_FAIL" }).Count -gt 0) {
	Write-Result "Instance benchmark ($Driver, $RenderThread)" $false "exit $code; see devtools/out/instance_bench"
	exit 1
}
Write-Result "Instance benchmark ($Driver, $RenderThread)" $true "$expected rows in devtools/out/instance_bench"
