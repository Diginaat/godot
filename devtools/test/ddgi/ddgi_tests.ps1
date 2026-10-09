# DDGI measurements with misc\ddgi_test_project (details: DDGI.md). Each test
# prints its numbers and leaves its images in devtools\out\ddgi\<test>.
#
#   devtools\test\ddgi\ddgi_tests.ps1 -Test flicker   # static interior: brightness and flicker
#   devtools\test\ddgi\ddgi_tests.ps1 -Test switch    # lights switched: how fast the GI follows
#   devtools\test\ddgi\ddgi_tests.ps1 -Test weather   # a storm rolls in: patchiness and lag
#   devtools\test\ddgi\ddgi_tests.ps1 -Test scroll    # camera moving at 8 m/s: error left by scrolling
#   devtools\test\ddgi\ddgi_tests.ps1 -Test bench     # GPU time of every DDGI pass, 1080p
#   devtools\test\ddgi\ddgi_tests.ps1 -Test bake      # bake the interior, load it baked
#   devtools\test\ddgi\ddgi_tests.ps1 -Test compare   # the README comparison shots
#   devtools\test\ddgi\ddgi_tests.ps1 -Test all
#   ... -Extra "--quality=3","--realtime=1"   extra test project arguments
param(
	[ValidateSet("flicker", "switch", "weather", "scroll", "bench", "bake", "compare", "all")]
	[string]$Test = "flicker",
	[string[]]$Extra = @()
)
. "$PSScriptRoot\..\..\common.ps1"
$editor = Get-Editor
$project = Join-Path $RepoRoot "misc\ddgi_test_project"
$eval = Join-Path $PSScriptRoot "ddgi_eval.py"
# The engine and Python write to stderr; their output is checked, not trusted to stop the script.
$ErrorActionPreference = "Continue"

function Godot([string[]]$GameArgs) {
	# The engine writes warnings to stderr; Windows PowerShell would turn them
	# into terminating errors under "Stop". The output is checked below instead.
	$ErrorActionPreference = "Continue"
	$lines = & $editor --path $project -- @GameArgs 2>&1 | ForEach-Object { "$_" }
	$errors = Get-RealErrors $lines
	if ($errors.Count -gt 0) { Write-Host "   $($errors.Count) errors, first: $($errors[0])" -ForegroundColor Red }
	return $lines
}
function New-OutDir($name) { $d = Join-Path $Out "ddgi\$name"; New-Item -ItemType Directory -Force $d | Out-Null; return $d }

$tests = if ($Test -eq "all") { @("flicker", "switch", "weather", "scroll", "bench", "bake") } else { @($Test) }
foreach ($t in $tests) {
	Write-Host "== $t" -ForegroundColor Cyan
	switch ($t) {
		"flicker" {
			$d = New-OutDir "flicker"
			Godot (@("--view=interior", "--gi=ddgi", "--tod=0", "--anim=0", "--res=640x360", "--linear", "--exposure=4.0", "--frames=600", "--measure=60", "--mean=$d\mean.png", "--stdmap=$d\flicker_map.png") + $Extra) | Select-String "^MEASURE" | ForEach-Object { $_.Line }
		}
		"switch" {
			$d = New-OutDir "switch"
			Godot (@("--view=room", "--gi=ddgi", "--anim=0", "--res=640x360", "--frames=400", "--debug=1", "--switch", "--after=1,3,5,10,20,40,80,400,1200", "--shot=$d\s.png") + $Extra) | Out-Null
			python $eval switch $d
		}
		"weather" {
			foreach ($view in @("outdoor", "interior")) {
				$d = New-OutDir "weather_$view"
				Get-ChildItem $d -Filter "f*.png" -ErrorAction SilentlyContinue | Remove-Item
				Godot (@("--view=$view", "--gi=ddgi", "--tod=0", "--anim=0", "--res=640x360", "--debug=1", "--frames=600", "--weather=$d") + $Extra) | Out-Null
				Write-Host "   ${view}: $(python $eval weather $d)"
			}
		}
		"scroll" {
			$d = New-OutDir "scroll"
			foreach ($i in 1..3) {
				Godot (@("--view=outdoor", "--gi=ddgi", "--move=1", "--speed=8", "--debug=1", "--res=1280x720", "--frames=600", "--shot=$d\run$i.png", "--settle=600") + $Extra) | Out-Null
				Write-Host "   run ${i}: $(python $eval scroll "$d\run$i.png" "$d\run${i}_settled.png")"
			}
		}
		"bench" {
			foreach ($view in @("room", "outdoor", "stress")) {
				foreach ($quality in 0..3) {
					$line = & $editor --gpu-profile --path $project -- (@("--view=$view", "--quality=$quality", "--res=1920x1080", "--frames=300", "--bench=300") + $Extra) 2>&1 | ForEach-Object { "$_" } | Where-Object { $_ -match "^BENCH" }
					Write-Host "   $line"
				}
			}
		}
		"bake" {
			$d = New-OutDir "bake"
			$volume = @("--view=interior", "--gi=ddgi", "--tod=0", "--anim=0", "--res=640x360", "--linear", "--exposure=4.0", "--volume=400,1.5,0,13,3.6,11", "--vspacing=0.5")
			Godot ($volume + @("--bake=$d\interior.ddgi.res", "--frames=1", "--measure=2") + $Extra) | Select-String "^BAKE" | ForEach-Object { "   " + $_.Line }
			Write-Host "   dynamic, first frames:  $(Godot ($volume + @('--frames=2', '--measure=3')) | Select-String '^MEASURE')"
			Write-Host "   baked, first frames:    $(Godot ($volume + @("--baked=$d\interior.ddgi.res", '--bake_mode=1', '--frames=2', '--measure=3')) | Select-String '^MEASURE')"
		}
		"compare" {
			# README comparison: no GI, DDGI, DDGI + SSAO/SSIL/SSR, + DLSS Quality.
			$d = New-OutDir "compare"
			$exposure = @{ "outdoor" = @("--tonemap_exposure=1.0"); "interior" = @("--tonemap_exposure=6", "--bounce=1.5"); "room" = @("--tonemap_exposure=1.0") }
			$sheet = @()
			foreach ($view in @("outdoor", "interior", "room")) {
				$base = @("--view=$view", "--tod=0", "--anim=0", "--res=1280x720", "--frames=900", "--quality=2") + $exposure[$view]
				Godot ($base + @("--gi=none", "--shot=$d\${view}_off.png")) | Out-Null
				Godot ($base + @("--gi=ddgi", "--shot=$d\${view}_ddgi.png")) | Out-Null
				Godot ($base + @("--gi=ddgi", "--ssao=1", "--ssil=1", "--ssr=1", "--shot=$d\${view}_ddgi_fx.png")) | Out-Null
				Godot ($base + @("--gi=ddgi", "--ssao=1", "--ssil=1", "--ssr=1", "--scale3d=6", "--scale=0.67", "--shot=$d\${view}_ddgi_fx_dlss.png")) | Out-Null
				$sheet += @("$d\${view}_off.png", "$d\${view}_ddgi.png", "$d\${view}_ddgi_fx.png", "$d\${view}_ddgi_fx_dlss.png")
			}
			python $eval grid "$d\contact_sheet.png" @sheet
		}
	}
}
