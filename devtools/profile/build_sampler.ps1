# Builds devtools/profile/sampler.cpp with MSVC into devtools/out/profile/sampler.exe.
$ErrorActionPreference = "Stop"
$out = Join-Path $PSScriptRoot "..\out\profile"
New-Item -ItemType Directory -Force $out | Out-Null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$source = Join-Path $PSScriptRoot "sampler.cpp"
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>nul && cd /d `"$out`" && cl /nologo /O2 /EHsc /std:c++17 `"$source`" /Fe:sampler.exe winmm.lib dbghelp.lib"
if ($LASTEXITCODE -ne 0) { throw "sampler build failed" }
Join-Path $out "sampler.exe"
