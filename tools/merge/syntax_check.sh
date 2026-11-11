#!/usr/bin/env bash
# Compile each given .cpp on its own with -fsyntax-only, reusing the flags of the
# configured unity build. Unity batches hide missing includes; upstream's test preset
# (linux-debug) and vcproj/canary.vcxproj build without unity.
#
#   tools/merge/syntax_check.sh build/linux-release-enabled-tests $(git diff --name-only ORIG_HEAD -- 'src/*.cpp')
set -euo pipefail

build_dir=$1
shift
root=$(git rev-parse --show-toplevel)
command=$(cd "$build_dir" && ninja -t commands canary_core | grep -m1 'Unity/unity_.*_cxx\.cxx')
compiler=${command%% *}
flags=$(sed -E 's| -o [^ ]+||; s| -c [^ ]+||; s| -MD||; s| -MT [^ ]+||; s| -MF [^ ]+||' <<<"${command#* }")

failed=0
for file in "$@"; do
	[[ $file == *.cpp && -f $root/$file ]] || continue
	# shellcheck disable=SC2086 # flags must word-split
	if ! output=$(cd "$build_dir" && $compiler $flags -fsyntax-only "$root/$file" 2>&1); then
		failed=$((failed + 1))
		echo "## $file"
		grep -m5 'error:' <<<"$output" || true
	fi
done
echo "files with errors: $failed"
[[ $failed -eq 0 ]]
