# Sets up a Windows PC for developing this Godot build, in one go. Safe to run
# again: every step that is already done is skipped.
#
#   1. Checks the tools: Git, Python 3, Visual Studio 2022 C++ (or the Build
#      Tools), CMake; the CUDA Toolkit for PhysX GPU dynamics (optional) and
#      the .NET SDK for the C# editor (only with -Mono).
#   2. Installs SCons (pip) and Godot's D3D12 build dependencies.
#   3. Sets the git remotes the runbook (CUSTOM_BUILD.md) uses: upstream,
#      origin (NVIDIA-RTX), physx and fork (Diginaat).
#   4. Builds the PhysX, Blast and Flow SDKs once, in a physx-sdk folder next
#      to this repository (about 20 minutes the first time).
#   5. Writes custom.py (not tracked) with the SDK paths, so a build is just:
#        python -m SCons platform=windows target=editor production=yes
#   6. With -Build, builds the editor (and with -Mono the C# editor too).
#
# Usage, from the repository root in PowerShell (not Git Bash):
#   powershell -ExecutionPolicy Bypass -File devtools\setup\setup_dev_windows.ps1
#   powershell -ExecutionPolicy Bypass -File devtools\setup\setup_dev_windows.ps1 -Build
#   powershell -ExecutionPolicy Bypass -File devtools\setup\setup_dev_windows.ps1 -Build -Mono
#   ... -NoGpu         PhysX without CUDA GPU dynamics
#   ... -PhysXDir D:\sdk\physx-sdk   build the SDKs somewhere else
param(
	[switch]$Build,
	[switch]$Mono,
	[switch]$NoGpu,
	[string]$PhysXDir = ""
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
Set-Location $root
if (-not $PhysXDir) { $PhysXDir = Join-Path (Split-Path $root -Parent) "physx-sdk" }

function Step($text) { Write-Host ""; Write-Host "== $text" -ForegroundColor Cyan }
function Ok($text) { Write-Host "   OK  $text" -ForegroundColor Green }
function Info($text) { Write-Host "   ..  $text" }
function Fail($text) { Write-Host "   !!  $text" -ForegroundColor Red; exit 1 }
function Have($command) { return [bool](Get-Command $command -ErrorAction SilentlyContinue) }

# 1. Tools.
Step "Checking the tools"
if (-not (Have git)) { Fail "Git is missing. Install it: winget install Git.Git" }
Ok (git --version)
if (-not (Have python)) { Fail "Python 3 is missing. Install it: winget install Python.Python.3.12" }
$pyVersion = (python -c "import sys; print('%d.%d' % sys.version_info[:2])")
if ([version]$pyVersion -lt [version]"3.8") { Fail "Python $pyVersion is too old; Godot needs 3.8 or newer." }
Ok "Python $pyVersion"

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vs = if (Test-Path $vswhere) { & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath } else { $null }
if (-not $vs) { Fail "Visual Studio 2022 with the C++ workload is missing. Install it: winget install Microsoft.VisualStudio.2022.BuildTools --override ""--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive""" }
Ok "Visual Studio C++: $vs"

if (-not (Have cmake)) {
	$vsCmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
	if (Test-Path (Join-Path $vsCmake "cmake.exe")) {
		$env:PATH = "$vsCmake;$env:PATH"
		Ok "CMake (from Visual Studio)"
	} else {
		Fail "CMake is missing. Install it: winget install Kitware.CMake"
	}
} else {
	Ok (cmake --version | Select-Object -First 1)
}

$gpu = -not $NoGpu
if ($gpu) {
	$nvcc = if ($env:CUDA_PATH -and (Test-Path (Join-Path $env:CUDA_PATH "bin\nvcc.exe"))) { Join-Path $env:CUDA_PATH "bin\nvcc.exe" } elseif (Have nvcc) { (Get-Command nvcc).Source } else { $null }
	if ($nvcc) {
		Ok "CUDA Toolkit: $nvcc"
	} else {
		$gpu = $false
		Info "No CUDA Toolkit found: PhysX is built without GPU dynamics. For GPU dynamics install it (winget install Nvidia.CUDA --version 12.8), open a new terminal and run this script again."
	}
}
if ($Mono) {
	if (-not (Have dotnet)) { Fail ".NET SDK 8 or newer is missing (needed for -Mono). Install it: winget install Microsoft.DotNet.SDK.8" }
	Ok ".NET SDK $(dotnet --version)"
}

# 2. SCons and the D3D12 dependencies.
Step "Installing SCons and the D3D12 build dependencies"
python -m SCons --version *> $null
if ($LASTEXITCODE -ne 0) {
	Info "Installing SCons"
	python -m pip install --user scons
	if ($LASTEXITCODE -ne 0) { Fail "pip couldn't install SCons." }
}
Ok "SCons $((python -m SCons --version | Select-String 'SCons: v' | Select-Object -First 1) -replace '.*v', '')"
$deps = Join-Path $env:LOCALAPPDATA "Godot\build_deps"
if ((Test-Path (Join-Path $deps "agility_sdk")) -and (Get-ChildItem $deps -Directory -Filter "mesa-*" -ErrorAction SilentlyContinue)) {
	Ok "D3D12 dependencies in $deps"
} else {
	Info "Downloading the D3D12 dependencies (Mesa NIR, Agility SDK, PIX)"
	python misc/scripts/install_d3d12_sdk_windows.py
	if ($LASTEXITCODE -ne 0) { Fail "install_d3d12_sdk_windows.py failed." }
	Ok "D3D12 dependencies installed"
}

# 3. Remotes (names as in CUSTOM_BUILD.md).
Step "Setting the git remotes"
$want = [ordered]@{
	"upstream" = "https://github.com/godotengine/godot.git"
	"origin" = "https://github.com/NVIDIA-RTX/godot.git"
	"physx" = "https://github.com/uno1982/godot.git"
	"fork" = "https://github.com/Diginaat/godot.git"
}
$remotes = @(git remote)
# A fresh clone of the fork calls it origin; the runbook calls it fork and
# keeps origin for NVIDIA's repository.
if (($remotes -contains "origin") -and -not ($remotes -contains "fork") -and ((git remote get-url origin) -match "Diginaat/godot")) {
	git remote rename origin fork
	Info "Renamed the remote 'origin' (Diginaat/godot) to 'fork'"
	$remotes = @(git remote)
}
foreach ($name in $want.Keys) {
	if ($remotes -contains $name) {
		Ok "$name -> $(git remote get-url $name)"
	} else {
		git remote add $name $want[$name]
		Ok "$name -> $($want[$name]) (added)"
	}
}

# 4. PhysX, Blast and Flow SDKs.
Step "PhysX, Blast and Flow SDKs"
$preset = if ($gpu) { "vc17win64-godot-gpu" } else { "vc17win64-godot" }
$physxSdk = Join-Path $PhysXDir "physx\install\$preset\PhysX"
$blastSdk = Join-Path $PhysXDir "blast\_build\windows-x86_64\release\blast-sdk"
$flowSdk = Join-Path $PhysXDir "flow"
$flowBuilt = Test-Path (Join-Path $flowSdk "_build\windows-x86_64\release\nvflow.dll")
if ((Test-Path $physxSdk) -and (Test-Path $blastSdk) -and $flowBuilt) {
	Ok "Already built in $PhysXDir"
} else {
	Info "Building them in $PhysXDir (the first time takes a while)"
	$physxArgs = @("modules/godot_physx/misc/build_physx.py", "--blast", "--flow", "--src", $PhysXDir)
	if ($gpu) { $physxArgs += "--gpu" }
	python @physxArgs
	if ($LASTEXITCODE -ne 0) { Fail "build_physx.py failed; see its output above." }
	foreach ($path in @($physxSdk, $blastSdk)) { if (-not (Test-Path $path)) { Fail "Expected $path after the SDK build." } }
	Ok "Built in $PhysXDir"
}

# 5. custom.py: the build options of this PC (not tracked by git).
Step "Writing custom.py"
$custom = @"
# Build options of this PC, written by devtools/setup/setup_dev_windows.ps1.
# SCons reads this file on every build; it is not tracked by git.
physx_sdk = r"$physxSdk"
physx_gpu = "$(if ($gpu) { 'yes' } else { 'no' })"
blast_sdk = r"$blastSdk"
flow_sdk = r"$flowSdk"
"@
Set-Content -Path (Join-Path $root "custom.py") -Value $custom -Encoding ascii
Ok "custom.py: PhysX $(if ($gpu) { '(GPU)' } else { '(CPU only)' }), Blast, Flow"

# 6. Optional build.
if ($Build) {
	Step "Building the editor"
	python -m SCons platform=windows target=editor production=yes
	if ($LASTEXITCODE -ne 0) { Fail "The editor build failed." }
	Ok "bin\godot.windows.editor.x86_64.exe"
	if ($Mono) {
		Step "Building the C# (.NET) editor"
		python -m SCons platform=windows target=editor production=yes module_mono_enabled=yes
		if ($LASTEXITCODE -ne 0) { Fail "The .NET editor build failed." }
		& bin\godot.windows.editor.x86_64.mono.console.exe --headless --generate-mono-glue modules\mono\glue
		python modules/mono/build_scripts/build_assemblies.py --godot-output-dir=./bin --godot-platform=windows
		if ($LASTEXITCODE -ne 0) { Fail "Building the C# assemblies failed." }
		Ok "bin\godot.windows.editor.x86_64.mono.exe and bin\GodotSharp"
	}
}

Step "Done"
Write-Host "   Build the editor:   python -m SCons platform=windows target=editor production=yes"
Write-Host "   .NET editor:        add module_mono_enabled=yes (see README.md, step 6)"
Write-Host "   NVIDIA DLSS:        in the editor, click 'Get NVIDIA DLSS...' (optional)"
Write-Host "   Maintenance:        CUSTOM_BUILD.md"
