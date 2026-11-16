param(
	# Scenarios of scene_bench.gd; default: all twelve.
	[string[]]$Scenarios = @("idle", "deep", "wide", "parents", "sparse", "heavy", "script", "native", "churn", "reparent", "instance", "mixed"),
	[int[]]$Counts = @(1000, 10000, 100000),
	# Binaries to compare, alternated in each round (the first is the baseline);
	# names in bin\ (godot.windows.editor.x86_64.console.exe) or full paths.
	[string[]]$Builds = @(),
	[int]$Rounds = 3,
	[int]$Frames = 120,
	# MeshInstance3D instead of Node3D, with a window (transform changes reach the RenderingServer).
	[switch]$Visual,
	[string]$Label = "scene"
)
# SceneTree / Node / transform benchmark (docs/benchmarks/scene-runtime.md):
# every scenario and count in its own process, builds alternated per round
# (order reversed every other round). Writes devtools/out/scene_bench/<Label>.csv
# and prints medians per build plus the median of the per-round differences
# against the first build.
. "$PSScriptRoot\..\common.ps1"
$ErrorActionPreference = "Continue"
if ($Builds.Count -eq 0) { $Builds = @(Get-Editor) }
$resolved = @($Builds | ForEach-Object { if (Test-Path $_) { (Resolve-Path $_).Path } else { Join-Path $RepoRoot "bin\$_" } })
$project = Join-Path $RepoRoot "misc\scene_bench_project"
$script = Join-Path $PSScriptRoot "scene_bench.gd"
$outDir = Join-Path $Out "scene_bench"
New-Item -ItemType Directory -Force $outDir | Out-Null
$rows = @()
function Get-Value($p_values, [string]$p_key) { if ($p_values.ContainsKey($p_key)) { return [double]$p_values[$p_key] } else { return -1.0 } }
for ($r = 1; $r -le $Rounds; $r++) {
	foreach ($scenario in $Scenarios) {
		foreach ($count in $Counts) {
			$order = if ($r % 2 -eq 0) { $resolved[($resolved.Count - 1)..0] } else { $resolved }
			foreach ($exe in $order) {
				$engineArgs = @("--path", $project)
				if (-not $Visual) { $engineArgs = @("--headless") + $engineArgs }
				# 100k-node runs take up to a second or more per frame (instancing): fewer frames.
				$frames = if ($count -ge 100000) { [Math]::Min($Frames, 30) } else { $Frames }
				$warmup = if ($count -ge 100000) { 5 } else { 30 }
				$userArgs = @("--scenario=$scenario", "--count=$count", "--frames=$frames", "--warmup=$warmup")
				if ($Visual) { $userArgs += "--visual" }
				$lines = & $exe @engineArgs --script $script -- @userArgs 2>&1 | ForEach-Object { "$_" }
				$line = $lines | Where-Object { $_ -match "^SCENE_BENCH: " } | Select-Object -First 1
				$errors = @(Get-RealErrors $lines) + @($lines | Where-Object { $_ -match "SCRIPT ERROR|SCENE_BENCH_FAIL" })
				$values = @{}
				if ($line) { foreach ($m in [regex]::Matches($line, "(\w+)=([^ ]+)")) { $values[$m.Groups[1].Value] = $m.Groups[2].Value } }
				$rows += [pscustomobject]@{
					Round = $r; Scenario = $scenario; Count = $count; Build = (Split-Path $exe -Leaf)
					FrameMs = (Get-Value $values "frame_ms_p50"); FrameP95 = (Get-Value $values "frame_ms_p95")
					OpMs = (Get-Value $values "op_ms_p50"); SetupMs = (Get-Value $values "setup_ms"); StaticMb = (Get-Value $values "static_mb")
					Errors = $errors.Count
				}
				"[{0} r{1}] {2,-9} {3,6} {4}: frame {5} ms, op {6} ms, setup {7} ms, {8} MB{9}" -f $Label, $r, $scenario, $count, (Split-Path $exe -Leaf), $values["frame_ms_p50"], $values["op_ms_p50"], $values["setup_ms"], $values["static_mb"], $(if ($errors.Count) { ", $($errors.Count) errors: $($errors[0])" } else { "" })
			}
		}
	}
}
$rows | Export-Csv (Join-Path $outDir "$Label.csv") -NoTypeInformation
function Median([double[]]$v) { $s = @($v | Sort-Object); if ($s.Count -eq 0) { return [double]::NaN }; return $s[[int](($s.Count - 1) / 2)] }
""
"median of $Rounds rounds (ms per frame; op = the scenario's own operations); diff = median of per-round differences against $(Split-Path $resolved[0] -Leaf)"
foreach ($scenario in $Scenarios) {
	foreach ($count in $Counts) {
		$base = @($rows | Where-Object { $_.Scenario -eq $scenario -and $_.Count -eq $count -and $_.Build -eq (Split-Path $resolved[0] -Leaf) })
		foreach ($exe in $resolved) {
			$name = Split-Path $exe -Leaf
			$mine = @($rows | Where-Object { $_.Scenario -eq $scenario -and $_.Count -eq $count -and $_.Build -eq $name -and $_.FrameMs -ge 0 })
			if ($mine.Count -eq 0) { continue }
			$diffs = @($mine | ForEach-Object { $round = $_.Round; $b = $base | Where-Object { $_.Round -eq $round } | Select-Object -First 1; if ($b) { $_.FrameMs - $b.FrameMs } })
			"{0,-9} {1,6} {2,-45} frame {3,9:N3} ({4:N3}-{5:N3})  op {6,9:N3}  diff {7,8:N3}" -f $scenario, $count, $name, (Median ($mine.FrameMs)), ($mine.FrameMs | Measure-Object -Minimum).Minimum, ($mine.FrameMs | Measure-Object -Maximum).Maximum, (Median ($mine.OpMs)), (Median $diffs)
		}
	}
}
