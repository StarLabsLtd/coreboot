#!/bin/bash
set -euo pipefail
recipe=/home/sean/host683-lint-evidence-collector.4055rz
destination=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/stage680-lexical683-and-full-lint
test ! -e "$destination" && test ! -L "$destination"
stage=/home/sean/stage-code-decorators-final.20261004-r1
first=/home/sean/lint-lexical-followup-host.tSu0d4
retry=/home/sean/lint-lexical-followup-host-retry.JmMduj
full=/home/sean/full-lint-after683-final.20261004-r1
status_is()
{
	test "$(cat "$1/$2.status")" = "$3"
}
status_count()
{
	test "$(find "$1" -maxdepth 1 -type f -name '*.status' | wc -l)" = "$2"
}
status_count "$stage" 9
status_count "$first" 13
status_count "$retry" 15
status_count "$full" 14
for directory in "$stage" "$retry"; do
	for name in execution closure aggregate; do status_is "$directory" "$name" 0; done
done
status_is "$stage" old-checkpatch 1
status_is "$stage" old-checkpatch-opposition 0
for name in actual-stage current-checkpatch token-equivalence sections-and-sanitizers; do
	status_is "$stage" "$name" 0
done
grep -Fxq 'Three exact decorator expansions, fourteen uses, complete C tokens identical' "$stage/token-equivalence.log"
for directory in "$first" "$retry"; do
	for name in syntax models actual-025-native-boundary actual-028-camelcase \
	    actual-016-non-ascii new-lexer old-lexer old-lexer-opposition; do
		status_is "$directory" "$name" 0
	done
	status_is "$directory" old-boundary 1
	for name in actual-025-native-boundary actual-028-camelcase actual-016-non-ascii; do
		test ! -s "$directory/$name.log"
	done
done
for name in execution aggregate old-boundary-opposition; do status_is "$first" "$name" 1; done
status_is "$first" closure 0
test ! -s "$first/old-boundary.log"
status_is "$retry" old-boundary-opposition 0
status_is "$retry" old-actual-boundary 0
status_is "$retry" old-actual-boundary-opposition 0
grep -Fx -f "$retry/old-actual-expected.txt" "$retry/old-actual-boundary.log" > /dev/null
status_is "$full" lint 2
for name in execution aggregate; do status_is "$full" "$name" 1; done
for name in candidate-signature producer-signature configure canonical-signature \
    private-signature config-match header-match compiler-inputs lint-extended \
    full-checkpatch closure; do status_is "$full" "$name" 0; done
test ! -s "$full/full-checkpatch.log"
grep -Fq 'ROADMAP.md:44:- Coreboot PR390' "$full/lint.log"
sha256sum --quiet -c <<'PINS'
d355931966703553491a82873e981d67e84cf6573c4715a9b02882337506f19e  /home/sean/stage-code-decorators-host-recipe.WecndG/run.sh
56b9c16243629ff12654cd8d11bb77c4a2f7b865eece6d0ae70421278f8d82dd  /home/sean/stage-code-decorators-host-recipe.WecndG/README.txt
827e515738d08dbccc6d1d7064ee595256d58fe875cf03c5604579ee30bbbebb  /home/sean/stage-code-decorators-host-recipe.WecndG/hash-inputs.py
1190d548fc3c6a99e80721ccae5a3a1ea547e849d006e5ba694d9fb92faeff4d  /home/sean/stage-code-decorators-host-recipe.WecndG/verify-tokens.pl
ce3956a501e9ee9b1b5a6c9c901c679288db4088c3301208f3aaf058ad16f4a2  /home/sean/lint-lexical-followup-host.tSu0d4/run.sh
ebf179e65585e465cd12920dbbeaf400e82893dd2e4c16df9b687ec9c52f46b4  /home/sean/lint-lexical-followup-host.tSu0d4/README.txt
d3e8de15e039333c2a797cb892ee093938ac19b15f5d7dc7591c4c95d19e20dc  /home/sean/lint-lexical-followup-host-retry.JmMduj/run.sh
a72b8c763e3292e6f75b0db0d072ca5bd8bfd619a0514fb51d1d337038358762  /home/sean/lint-lexical-followup-host-retry.JmMduj/README.txt
990b5b30fec42709fffb9db39dbedec2488add8cb13370ba449ecc9275f7e181  /home/sean/full-lint-after683-recipe.yBUFyN/run.sh
048a5a6f24f6284b127d9cb699bb8e1a19a5f84ebb5ba866b99aa8be15759290  /home/sean/full-lint-after683-recipe.yBUFyN/README.txt
827e515738d08dbccc6d1d7064ee595256d58fe875cf03c5604579ee30bbbebb  /home/sean/full-lint-after683-recipe.yBUFyN/hash-inputs.py
PINS
mkdir "$destination"
: > "$destination/ORIGINAL_FILES.sha256"
: > "$destination/FILES_MAP.tsv"
copy_file()
{
	local label=$1 path=$2 target
	test -f "$path" && test ! -L "$path"
	target=$destination/$label/${path##*/}
	test ! -e "$target" && test ! -e "$target.gz"
	mkdir -p "$(dirname "$target")"
	sha256sum "$path" >> "$destination/ORIGINAL_FILES.sha256"
	if test "$(stat -c %s "$path")" -gt 1048576; then
		gzip -n -c "$path" > "$target.gz"
		gzip -dc "$target.gz" | cmp "$path" -
		target=$target.gz
	else
		cp -p "$path" "$target"
		cmp "$path" "$target"
	fi
	printf '%s\t%s\n' "$path" "${target#"$destination/"}" >> "$destination/FILES_MAP.tsv"
}
collect_root()
{
	local label=$1 directory=$2 path
	while IFS= read -r -d '' path; do
		case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
			copy_file "$label" "$path" ;;
		esac
	done < <(find "$directory" -maxdepth 1 -type f -print0 | sort -z)
}
collect_root stage680 "$stage"
collect_root lexical-first-failed "$first"
collect_root lexical-retry "$retry"
collect_root full-lint683-failed "$full"
copy_file lexical-first-failed "$first/run.sh"
copy_file lexical-retry "$retry/run.sh"
for directory in /home/sean/stage-code-decorators-host-recipe.WecndG \
    /home/sean/full-lint-after683-recipe.yBUFyN; do
	case "$directory" in *WecndG) label=stage680-recipe ;; *) label=full-lint683-recipe ;; esac
	for file in run.sh README.txt hash-inputs.py; do copy_file "$label" "$directory/$file"; done
done
copy_file stage680-recipe /home/sean/stage-code-decorators-host-recipe.WecndG/verify-tokens.pl
sha256sum --quiet -c "$destination/ORIGINAL_FILES.sha256"
cp -p "$recipe/collect.sh" "$destination/collector.sh"
cp -p "$recipe/README.md" "$destination/README.md"
cmp "$recipe/collect.sh" "$destination/collector.sh"
cmp "$recipe/README.md" "$destination/README.md"
(
	cd "$destination"
	find . -type f ! -name ARCHIVE.sha256 -print0 | sort -z | xargs -0 sha256sum > ARCHIVE.sha256
	sha256sum --quiet -c ARCHIVE.sha256
)
