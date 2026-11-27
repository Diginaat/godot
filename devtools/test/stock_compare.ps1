param(
	# Stock Godot console exe to compare against (for example Godot_v4.7.2-stable_win64_console.exe).
	[Parameter(Mandatory = $true)][string]$Stock,
	# This fork's console exe; default: bin\godot.windows.editor.x86_64.console.exe.
	[string]$Fork = "",
	[string[]]$Suites = @("scene", "level", "instance", "physics", "shader"),
	[int]$Rounds = 3,
	[string]$Label = "stock_compare"
)
# Stock Godot against this fork, on what the fork changed (docs/benchmarks/stock-compare.md):
#   scene    - SceneTree / Node / transform runtime (scene_bench.ps1, headless)
#   level    - game-level rendering: town, forest, city_extreme (misc/stock_compare_project/level_bench.gd)
#   instance - many-instance renderer cases (misc/large_world_test_project/instance_bench.gd)
#   physics  - rigid bodies: stock Jolt and GodotPhysics3D, fork PhysX GPU, PhysX CPU and Jolt
#   shader   - cold shader and pipeline compilation stall (shader_bench.gd)
# Each engine gets its own copy of the project (own .godot and user:// data).
# Builds alternate inside every round and the order flips every other round;
# one unrecorded run per exe first fills the driver's pipeline cache.
# Writes devtools/out/stock_compare/<Label>/results_<suite>.csv and prints medians.
. "$PSScriptRoot\..\common.ps1"
$ErrorActionPreference = "Continue"
# powershell -File passes "a,b" as one string.
$Suites = @($Suites | ForEach-Object { $_ -split "," } | Where-Object { $_ })
"suites: $($Suites -join ' '), rounds: $Rounds"
if (-not $Fork) { $Fork = Get-Editor }
$Stock = (Resolve-Path $Stock).Path
$Fork = (Resolve-Path $Fork).Path
$work = Join-Path $Out "stock_compare\$Label"
New-Item -ItemType Directory -Force $work | Out-Null
$src = Join-Path $RepoRoot "misc\stock_compare_project"
$lw = Join-Path $RepoRoot "misc\large_world_test_project"

# name -> exe, physics engine, extra override lines
$copies = [ordered]@{
	"stock" = @($Stock, "Jolt Physics", "")
	"fork" = @($Fork, "PhysX", "")
	"stock_godotphysics" = @($Stock, "GodotPhysics3D", "")
	"fork_physx_cpu" = @($Fork, "PhysX", "physics/physx_3d/simulation/gpu_dynamics=false")
	"fork_jolt" = @($Fork, "Jolt Physics", "")
}
foreach ($name in $copies.Keys) {
	$dir = Join-Path $work "proj_$name"
	if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
	New-Item -ItemType Directory -Force $dir | Out-Null
	Copy-Item "$src\*" $dir -Recurse
	foreach ($f in @("instance_bench.gd", "instance_bench.tscn", "shader_bench.gd", "shader_bench.tscn")) { Copy-Item (Join-Path $lw $f) $dir }
	$override = "[physics]`n3d/physics_engine=`"$($copies[$name][1])`"`n"
	if ($copies[$name][2]) {
		$kv = $copies[$name][2] -split "=", 2
		$override += "`n[$(($kv[0] -split '/')[0])]`n$($kv[0].Substring($kv[0].IndexOf('/') + 1))=$($kv[1])`n"
	}
	Set-Content -Path (Join-Path $dir "override.cfg") -Value $override -Encoding ascii
}

$rows = [System.Collections.ArrayList]::new()
function Invoke-Bench([string]$p_copy, [string]$p_suite, [string]$p_case, [string[]]$p_engine, [string[]]$p_user, [string]$p_tag, [int]$p_round) {
	$exe = $copies[$p_copy][0]
	$dir = Join-Path $work "proj_$p_copy"
	$argv = [string[]](@("--path", $dir) + $p_engine + @("--") + $p_user)
	$lines = & $exe @argv 2>&1 | ForEach-Object { "$_" }
	$lines | Set-Content (Join-Path $work "${p_suite}_${p_case}_${p_copy}_r$p_round.log")
	$errors = @(Get-RealErrors $lines) + @($lines | Where-Object { $_ -match "SCRIPT ERROR|_FAIL" })
	$hits = @($lines | Where-Object { $_ -match "^${p_tag}: " })
	if ($hits.Count -eq 0) { Write-Host "  [$p_suite $p_case $p_copy r$p_round] no $p_tag line; errors: $($errors | Select-Object -First 2)" -ForegroundColor Red }
	foreach ($hit in $hits) {
		$values = [ordered]@{ Suite = $p_suite; Case = $p_case; Copy = $p_copy; Round = $p_round; Errors = $errors.Count }
		foreach ($m in [regex]::Matches($hit, "(\w+)=('[^']*'|[^ ]+)")) { $values[$m.Groups[1].Value] = $m.Groups[2].Value }
		[void]$rows.Add([pscustomobject]$values)
	}
	return $hits
}
function Get-Order([string[]]$p_names, [int]$p_round) { if ($p_round % 2 -eq 0) { return $p_names[($p_names.Count - 1)..0] } return $p_names }

# Driver pipeline caches are per exe: one unrecorded rendering run each.
if ($Suites | Where-Object { $_ -in @("level", "instance") }) {
	foreach ($c in @("stock", "fork")) { Invoke-Bench $c "warmup" "town" @("--rendering-driver", "vulkan", "res://level_bench.tscn") @("--preset=town", "--frames=200", "--warmup=30") "LEVEL_BENCH" 0 | Out-Null }
}

for ($r = 1; $r -le $Rounds; $r++) {
	if ("level" -in $Suites) {
		foreach ($preset in @("town", "forest", "city_extreme")) {
			foreach ($c in (Get-Order @("stock", "fork") $r)) {
				$h = Invoke-Bench $c "level" $preset @("--rendering-driver", "vulkan", "--quit-after", "100000", "res://level_bench.tscn") @("--preset=$preset") "LEVEL_BENCH" $r
				Write-Host "  [level $preset $c r$r] $($h -replace '^LEVEL_BENCH: ', '' -replace ' adapter=.*? gpu_instances', ' gpu_instances')"
			}
		}
	}
	if ("instance" -in $Suites -and $r -le 2) {
		foreach ($c in (Get-Order @("stock", "fork") $r)) {
			# The bench sets the fork's GPU-path settings itself; these flags keep the fork's defaults on.
			$user = if ($c -eq "fork") { @("--table", "--gpu-instances", "--occlusion") } else { @() }
			$h = Invoke-Bench $c "instance" "all" @("--rendering-driver", "vulkan", "res://instance_bench.tscn") $user "INSTANCE_BENCH" $r
			Write-Host "  [instance $c r$r] $($h.Count) cases"
		}
	}
	if ("physics" -in $Suites) {
		foreach ($preset in @("crates_2k", "mixed_8k", "extreme_30k")) {
			$ticks = if ($preset -eq "extreme_30k") { 300 } else { 600 }
			foreach ($c in (Get-Order @("stock", "stock_godotphysics", "fork", "fork_physx_cpu", "fork_jolt") $r)) {
				$h = Invoke-Bench $c "physics" $preset @("--headless", "--fixed-fps", "60", "res://physics_bench.tscn") @("--preset=$preset", "--ticks=$ticks") "PHYSICS_BENCH" $r
				Write-Host "  [physics $preset $c r$r] $($h -replace '^PHYSICS_BENCH: ', '')"
			}
		}
	}
	if ("shader" -in $Suites -and $r -le 2) {
		foreach ($c in (Get-Order @("stock", "fork") $r)) {
			$userDir = Join-Path $env:APPDATA "Godot\app_userdata\Stock Compare Bench"
			foreach ($d in @((Join-Path $userDir "shader_cache"), (Join-Path $userDir "vulkan"), (Join-Path $env:LOCALAPPDATA "NVIDIA\GLCache"), (Join-Path $env:LOCALAPPDATA "AMD\VkCache"), (Join-Path $env:LOCALAPPDATA "Intel\ShaderCache"))) {
				if (Test-Path $d) { Get-ChildItem $d -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue }
			}
			$h = Invoke-Bench $c "shader" "cold120" @("--clear-shader-cache", "--rendering-driver", "vulkan", "--quit-after", "20000", "res://shader_bench.tscn") @("--count=120") "SHADER_BENCH" $r
			Write-Host "  [shader cold $c r$r] $($h -replace '^SHADER_BENCH: ', '')"
		}
	}
}
# Export-Csv takes its columns from the first row: one file per suite.
foreach ($suite in @($rows | ForEach-Object { $_.Suite } | Select-Object -Unique)) { $rows | Where-Object { $_.Suite -eq $suite } | Export-Csv (Join-Path $work "results_$suite.csv") -NoTypeInformation }

if ("scene" -in $Suites) {
	& (Join-Path $PSScriptRoot "scene_bench.ps1") -Builds @($Stock, $Fork) -Counts @(10000, 100000) -Rounds $Rounds -Label "${Label}_scene"
}

function Median($p_values) { $s = @($p_values | ForEach-Object { [double]$_ } | Sort-Object); if ($s.Count -eq 0) { return [double]::NaN }; return $s[[int](($s.Count - 1) / 2)] }
""
"Medians over rounds (results_<suite>.csv has every run). ratio = stock / fork (above 1: fork faster)."
$metrics = @{ level = @("wall_p50", "wall_p99", "cpu_p50", "gpu_p50", "draws_p50", "build_ms", "vram_mb"); instance = @("frame_p50", "frame_p99", "render_cpu_p50", "gpu_p50", "draws", "video_mib"); physics = @("tick_mean_ms", "tick_p95", "fall_mean_ms", "settle_mean_ms", "build_ms", "lost", "mean_y"); shader = @("spawn_frame_ms", "settle_ms", "max_ms", "frame_p50", "vram_mb") }
foreach ($suite in @("level", "instance", "physics", "shader")) {
	$sub = @($rows | Where-Object { $_.Suite -eq $suite })
	if ($sub.Count -eq 0) { continue }
	"== $suite"
	$keys = @($sub | ForEach-Object { if ($suite -eq "instance") { "$($_.case) $($_.view)" } else { $_.Case } } | Select-Object -Unique)
	foreach ($key in $keys) {
		$set = @($sub | Where-Object { $(if ($suite -eq "instance") { "$($_.case) $($_.view)" } else { $_.Case }) -eq $key })
		foreach ($c in @($set.Copy | Select-Object -Unique)) {
			$mine = @($set | Where-Object { $_.Copy -eq $c })
			$parts = foreach ($m in $metrics[$suite]) { "{0}={1:N3}" -f $m, (Median ($mine | ForEach-Object { $_.$m })) }
			"  {0,-22} {1,-20} {2}" -f $key, $c, ($parts -join " ")
		}
		$s = @($set | Where-Object { $_.Copy -eq "stock" }); $f = @($set | Where-Object { $_.Copy -eq "fork" })
		if ($s.Count -and $f.Count) {
			$m = $metrics[$suite][0]
			"  {0,-22} {1,-20} {2}: {3:N2}x" -f $key, "ratio", $m, ((Median ($s | ForEach-Object { $_.$m })) / [Math]::Max(1e-6, (Median ($f | ForEach-Object { $_.$m }))))
		}
	}
}
