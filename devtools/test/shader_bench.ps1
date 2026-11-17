param(
	[ValidateSet("vulkan", "d3d12")]
	[string]$Driver = "vulkan",
	# Editor copy to run: bin\godot<_Build>.windows.editor.x86_64.console.exe ("" = current build).
	[string]$Build = "",
	[string]$Label = "run",
	[int]$Count = 120,
	[int]$Repeats = 3,
	# Motion vectors (TAA) and separate specular (SSR) turn on more shader variants.
	[switch]$Mv,
	[switch]$Ssr,
	# Cold: clear every shader and pipeline cache before every run (see Clear-ShaderCaches).
	[switch]$Cold
)
# Shader and pipeline compilation benchmark (docs/shader-permutations/memory.md).
# Runs misc/large_world_test_project/shader_bench.tscn with --verbose and
# prints the scene's SHADER_BENCH line plus the engine's SHADER_STATS TOTAL,
# scene shader and PIPELINE_STATS lines per run.
. "$PSScriptRoot\..\common.ps1"
$exe = if ($Build) { Join-Path $RepoRoot "bin\godot_$Build.windows.editor.x86_64.console.exe" } else { Get-Editor }
if (-not (Test-Path $exe)) { throw "Missing $exe" }
$project = Join-Path $RepoRoot "misc\large_world_test_project"
$logs = Join-Path $Out "shader_bench"
New-Item -ItemType Directory -Force $logs | Out-Null
$userDir = Join-Path $env:APPDATA "Godot\app_userdata\Native Large World Rendering Tests"
$ErrorActionPreference = "Continue"

# Every cache layer that can make a "cold" run warm:
# - Godot's shader cache (user://shader_cache) and the baked read-only one
#   (res://.godot/shader_cache, used as a fallback);
# - Godot's driver pipeline cache (user://vulkan/pipelines.*);
# - the GPU driver's own disk caches, which outlive both (NVIDIA GLCache is
#   Vulkan/GL, DXCache and D3DSCache are D3D12; AMD and Intel equivalents).
# The driver caches are shared by every application: they refill on next use.
$cacheDirs = @(
	(Join-Path $userDir "shader_cache"),
	(Join-Path $userDir "vulkan"),
	(Join-Path $project ".godot\shader_cache"),
	(Join-Path $env:LOCALAPPDATA "NVIDIA\GLCache"),
	(Join-Path $env:LOCALAPPDATA "NVIDIA\DXCache"),
	(Join-Path $env:LOCALAPPDATA "D3DSCache"),
	(Join-Path $env:LOCALAPPDATA "AMD\VkCache"),
	(Join-Path $env:LOCALAPPDATA "AMD\DxCache"),
	(Join-Path $env:LOCALAPPDATA "AMD\DxcCache"),
	(Join-Path $env:LOCALAPPDATA "Intel\ShaderCache")
)
function Clear-ShaderCaches {
	$left = 0
	foreach ($dir in $cacheDirs) {
		if (-not (Test-Path $dir)) { continue }
		Get-ChildItem $dir -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
		# Files another running application holds open can't be deleted.
		$remaining = @(Get-ChildItem $dir -Recurse -File -Force -ErrorAction SilentlyContinue)
		if ($remaining.Count -gt 0) {
			Write-Host "    cache not empty: $dir ($($remaining.Count) files in use)"
			$left += $remaining.Count
		}
	}
	return $left
}

$userArgs = [string[]]@("--count=$Count")
if ($Mv) { $userArgs += "--mv" }
if ($Ssr) { $userArgs += "--ssr" }
$failed = $false
$baseArgs = [string[]](@("--verbose", "--path", $project, "--rendering-driver", $Driver, "--quit-after", "20000", "res://shader_bench.tscn", "--") + $userArgs)
if (-not $Cold) {
	# Warm runs: one unrecorded run first so every cache holds this build's shaders.
	& $exe @baseArgs 2>&1 | Out-Null
}
for ($r = 1; $r -le $Repeats; $r++) {
	$runArgs = $baseArgs
	if ($Cold) {
		$runArgs = [string[]](@("--clear-shader-cache") + $baseArgs)
		$left = Clear-ShaderCaches
		if ($left -gt 0) { Write-Host "    WARNING: $left cached files could not be cleared; close other GPU applications" }
	}
	$lines = & $exe @runArgs 2>&1 | ForEach-Object { "$_" }
	$code = $LASTEXITCODE
	$kind = $(if ($Cold) { "cold" } else { "warm" }) + $(if ($Mv) { "_mv" } else { "" }) + $(if ($Ssr) { "_ssr" } else { "" })
	$lines | Set-Content (Join-Path $logs "shader_bench_${Label}_${Driver}_${kind}_$r.log")
	$bench = @($lines | Where-Object { $_ -match "^SHADER_BENCH:" })
	Write-Output ("[$Label $Driver $kind #$r exit $code] " + ($bench -join " "))
	$lines | Where-Object { $_ -match "^SHADER_STATS: (TOTAL|SceneForwardClusteredShaderRD) |^PIPELINE_STATS:" } | ForEach-Object { Write-Output "    $_" }
	# Proof that a cold run was cold: no shader variant came from a cache.
	if ($Cold -and @($lines | Where-Object { $_ -match "^SHADER_STATS: TOTAL .* cached=0 " }).Count -ne 1) {
		Write-Output "    WARNING: cold run loaded shader variants from a cache"
		$failed = $true
	}
	if ($code -ne 0 -or $bench.Count -ne 1 -or @($lines | Where-Object { $_ -match "SCRIPT ERROR:" }).Count -gt 0) {
		$failed = $true
	}
}
if ($failed) {
	Write-Result "Shader benchmark ($Driver)" $false "see devtools/out/shader_bench"
	exit 1
}
Write-Result "Shader benchmark ($Driver)" $true "$Repeats runs in devtools/out/shader_bench"
