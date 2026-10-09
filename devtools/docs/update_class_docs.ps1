# Updates the class reference XML from the built editor (--doctool), so new
# classes, properties and methods get their entries, then lists what still
# needs a description and validates the XML.
#
#   devtools\docs\update_class_docs.ps1
#
# The doctool deletes the docs of classes this build doesn't compile in (for
# example PhysX classes behind a build option); those files are restored.
# Afterwards, check `git status`: upstream files the doctool changed for no
# reason of ours should be reverted, to keep upstream merges clean.
. "$PSScriptRoot\..\common.ps1"
Set-Location $RepoRoot
$editor = Get-Editor
$ErrorActionPreference = "Continue"  # The engine writes warnings to stderr.

& $editor --headless --doctool . 2>&1 | Out-Null
$deleted = @(git ls-files --deleted -- doc/classes "modules/*/doc_classes")
foreach ($f in $deleted) {
	git checkout -- $f
	Write-Host "restored $f (its class isn't in this build)"
}
git status --short -- doc modules/*/doc_classes

Write-Host "== Entries without a description" -ForegroundColor Cyan
python devtools/docs/check_class_docs.py -v
Write-Host "== Validating the XML" -ForegroundColor Cyan
python doc/tools/make_rst.py --dry-run doc/classes modules/ platform/ 2>&1 | Select-Object -Last 2
