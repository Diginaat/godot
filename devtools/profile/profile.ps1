param(
	[Parameter(Mandatory = $true)][string]$Label,
	# Arguments for the editor binary, e.g. --path <project> --script devtools/profile/core_workload.gd -- --mode=instance
	[Parameter(Mandatory = $true)][string[]]$GodotArgs,
	[double]$Warmup = 6,
	[double]$Seconds = 15,
	[int]$IntervalUs = 2000,
	# Binary with a PDB next to it (build with debug_symbols=yes).
	[string]$Editor = ""
)
# Starts the console editor, samples its engine process (the console exe is a
# wrapper that starts it) and writes devtools/out/profile/<Label>.tsv and .log.
. "$PSScriptRoot\..\common.ps1"
if (-not $Editor) { $Editor = Get-Editor }
$out = Join-Path $Out "profile"
New-Item -ItemType Directory -Force $out | Out-Null
$sampler = Join-Path $out "sampler.exe"
if (-not (Test-Path $sampler)) { & "$PSScriptRoot\build_sampler.ps1" | Out-Null }
$log = Join-Path $out "$Label.log"
$proc = Start-Process -FilePath $Editor -ArgumentList $GodotArgs -RedirectStandardOutput $log -RedirectStandardError "$log.err" -PassThru -WindowStyle Hidden
Start-Sleep -Seconds $Warmup
$child = Get-CimInstance Win32_Process -Filter "ParentProcessId=$($proc.Id)" | Where-Object { $_.Name -like "godot*" } | Select-Object -First 1
if ($proc.HasExited -or -not $child) {
	Get-Content $log, "$log.err" -ErrorAction SilentlyContinue | Select-Object -First 20
	throw "The engine process is not running (see $log)."
}
$target = $child.ProcessId
& $sampler $target $Seconds $IntervalUs (Join-Path $out "$Label.tsv") (Split-Path $Editor)
$proc.WaitForExit(120000) | Out-Null
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; if ($child) { Stop-Process -Id $child.ProcessId -Force -ErrorAction SilentlyContinue } }
Join-Path $out "$Label.tsv"
