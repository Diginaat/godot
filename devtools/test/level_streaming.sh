#!/usr/bin/env bash
# Level streaming acceptance test (docs/level-streaming.md), Linux.
#
# Imports and exports misc/level_streaming_test_project with the release
# template, copies the export to an output folder, makes the source project
# unreadable while the game runs (so the game can only use its own pack),
# and alternates streamed and fully preloaded runs.
#
#     devtools/test/level_streaming.sh [output-dir] [runs]
#
# The release template must be installed (export_templates/<version>/
# linux_release.x86_64, with the PhysX and Blast libraries next to it).
# Each run writes <output-dir>/<mode>_<n>.json and .log; the exit code is
# non-zero if any run failed.

set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PROJECT="$ROOT/misc/level_streaming_test_project"
EDITOR="$ROOT/bin/godot.linuxbsd.editor.x86_64"
OUT="${1:-$ROOT/bin/level_streaming_results}"
RUNS="${2:-3}"

mkdir -p "$OUT"
"$EDITOR" --headless --path "$PROJECT" --import > "$OUT/import.log" 2>&1
rm -rf "$PROJECT/build" && mkdir -p "$PROJECT/build"
"$EDITOR" --headless --path "$PROJECT" --export-release Linux build/level_streaming_test.x86_64 > "$OUT/export.log" 2>&1 || {
	echo "export failed, see $OUT/export.log"
	exit 1
}
rm -rf "$OUT/game" && cp -r "$PROJECT/build" "$OUT/game"

chmod 000 "$PROJECT"
trap 'chmod 755 "$PROJECT"' EXIT
status=0
for i in $(seq 1 "$RUNS"); do
	for mode in streamed preload_all; do
		args=()
		[ "$mode" = preload_all ] && args+=(--preload-all)
		(cd "$OUT/game" && timeout 300 ./level_streaming_test.x86_64 -- --results="$OUT/${mode}_$i.json" "${args[@]}") > "$OUT/${mode}_$i.log" 2>&1
		code=$?
		echo "$mode $i: exit $code, $(grep -m1 '^RESULT' "$OUT/${mode}_$i.log")"
		if grep -q "SCRIPT ERROR" "$OUT/${mode}_$i.log"; then
			echo "  script errors, see $OUT/${mode}_$i.log"
			code=1
		fi
		[ $code -ne 0 ] && status=1
	done
done
exit $status
