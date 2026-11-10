# Real-RD indirect MultiMesh foundation regression. No performance claim.
param(
	[ValidateSet("vulkan", "d3d12")]
	[string]$Driver = "vulkan"
)
. "$PSScriptRoot\..\common.ps1"
$editor = Get-Editor
$project = Join-Path $RepoRoot "misc\large_world_test_project"
$logs = Join-Path $Out "large_world"
New-Item -ItemType Directory -Force $logs | Out-Null
$ErrorActionPreference = "Continue"
$importLines = & $editor --headless --editor --import --path $project 2>&1 | ForEach-Object { "$_" }
$importCode = $LASTEXITCODE
$importLines | Set-Content (Join-Path $logs "scene_import.log")
if ($importCode -ne 0 -or @($importLines | Where-Object { $_ -match "ERROR:|SCRIPT ERROR:|leaked|ObjectDB instances" }).Count -gt 0) {
	$importLines | Write-Output
	Write-Result "Scene fixture import" $false "exit $importCode; see devtools/out/large_world"
	exit 1
}
foreach ($test in @("indirect", "culling", "clusters", "clod", "residency", "hzb", "hzb_two_pass", "multimesh_occlusion", "clod_raster", "import_clusters", "import_paged", "statistics", "multimesh_lod", "streaming", "streaming_disk", "voxel_proxy", "voxel_field", "grass_field")) {
	$scene = if ($test -eq "indirect") { "res://main.tscn" } else { "res://${test}.tscn" }
	$frameLimit = if ($test -eq "streaming" -or $test -eq "streaming_disk" -or $test -eq "import_paged" -or $test -eq "voxel_field") { 3000 } elseif ($test -eq "hzb_two_pass") { 4000 } elseif ($test -eq "multimesh_occlusion") { 3000 } elseif ($test -eq "clod_raster" -or $test -eq "multimesh_lod") { 600 } else { 300 }
	$lines = & $editor --path $project --rendering-driver $Driver --quit-after $frameLimit $scene 2>&1 | ForEach-Object { "$_" }
	$code = $LASTEXITCODE
	$lines | Set-Content (Join-Path $logs "${test}_$Driver.log")
	$errors = @($lines | Where-Object { $_ -match "ERROR:|SCRIPT ERROR:|TEST_FAIL|leaked|ObjectDB instances" })
	$marker = if ($test -eq "indirect") { "^INDIRECT_TEST_PASS:" } elseif ($test -eq "culling") { "^CULLING_TEST_PASS:" } elseif ($test -eq "clod") { "^CLOD_TEST_PASS:" } elseif ($test -eq "residency") { "^RESIDENCY_TEST_PASS:" } elseif ($test -eq "hzb") { "^HZB_TEST_PASS:" } elseif ($test -eq "hzb_two_pass") { "^HZB_TWO_PASS_TEST_PASS:" } elseif ($test -eq "multimesh_occlusion") { "^MULTIMESH_OCCLUSION_TEST_PASS:" } elseif ($test -eq "clod_raster") { "^CLOD_RASTER_TEST_PASS:" } elseif ($test -eq "import_clusters") { "^IMPORT_CLUSTER_TEST_PASS:" } elseif ($test -eq "import_paged") { "^IMPORT_PAGED_TEST_PASS:" } elseif ($test -eq "statistics") { "^STATISTICS_TEST_PASS:" } elseif ($test -eq "multimesh_lod") { "^MULTIMESH_LOD_TEST_PASS:" } elseif ($test -eq "streaming") { "^STREAMING_TEST_PASS:" } elseif ($test -eq "streaming_disk") { "^STREAMING_DISK_TEST_PASS:" } elseif ($test -eq "voxel_proxy") { "^VOXEL_PROXY_TEST_PASS:" } elseif ($test -eq "voxel_field") { "^VOXEL_FIELD_TEST_PASS:" } elseif ($test -eq "grass_field") { "^GRASS_FIELD_TEST_PASS:" } else { "^CLUSTER_TEST_PASS:" }
	$passed = $code -eq 0 -and $errors.Count -eq 0 -and ($lines -match $marker)
	if (-not $passed) {
		$lines | Write-Output
		Write-Result "Native $test ($Driver)" $false "exit $code; see devtools/out/large_world"
		exit 1
	}
	$detail = if ($test -eq "clusters") { "hierarchy, cuts and checked resource cache" } elseif ($test -eq "clod") { "GPU hierarchy cuts and bounded native commands" } elseif ($test -eq "residency") { "CPU/GPU coverage, fallback and page requests" } elseif ($test -eq "hzb") { "GPU min-depth mips, guarded visibility, two-pass kernels" } elseif ($test -eq "hzb_two_pass") { "two-pass depth: deferred groups, same-frame recovery, TAA/FSR2 jitter" } elseif ($test -eq "multimesh_occlusion") { "instance HZB occlusion: exact images, fully-inside MultiMesh, LOD levels, TAA" } elseif ($test -eq "import_clusters") { "real imported cache, sharing and fallback" } elseif ($test -eq "import_paged") { "imported page file: export listing, disk streaming, resident image" } elseif ($test -eq "statistics") { "exact async GPU geometry counters" } elseif ($test -eq "multimesh_lod") { "per-instance GPU LOD buckets and coverage" } elseif ($test -eq "streaming") { "pooled CLOD pages: convergence, pressure, fallback" } elseif ($test -eq "streaming_disk") { "CLOD pages from disk: convergence, reload, damaged and stale files, lifetime" } elseif ($test -eq "voxel_proxy") { "voxel statistics and far coverage/colour" } elseif ($test -eq "voxel_field") { "GPU voxel levels: exact selection, frustum, cap, coverage/colour" } elseif ($test -eq "grass_field") { "GPU blades: exact counts, heights, density, frustum, cap" } else { "GPU assertions and material pixels" }
	Write-Result "Native $test ($Driver)" $true $detail
}
# GPU instance table (Phase 1 of docs/renderer): GPU buffer against its CPU copy, plus a self-test.
& "$PSScriptRoot\instance_table.ps1" -Driver $Driver
if ($LASTEXITCODE -ne 0) {
	exit 1
}
# GPU path for eligible instances (Phase 2): counts and images against the CPU path, plus a self-test.
& "$PSScriptRoot\gpu_instances.ps1" -Driver $Driver
if ($LASTEXITCODE -ne 0) {
	exit 1
}
