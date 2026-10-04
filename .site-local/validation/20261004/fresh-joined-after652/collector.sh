#!/bin/bash
set -euo pipefail
destination=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/fresh-joined-after652
test ! -e "$destination"
mkdir -p "$destination"
copy_text() {
	local label=$1 source=$2 path relative
	while IFS= read -r -d '' path; do
		relative=${path#"$source"/}
		case "$relative" in build/*|base/*|base-build/*) continue ;; esac
		case "$relative" in
			*.log|*.txt|*.time|*.status|*.sha256|*.sh|*.md|*.patch|*.list|*.json|status|*-before|*-after)
				mkdir -p "$destination/$label/$(dirname "$relative")"
				cp -p "$path" "$destination/$label/$relative"
				cmp "$path" "$destination/$label/$relative"
				sha256sum "$path" >> "$destination/ORIGINAL_FILES.sha256"
				;;
		esac
	done < <(find "$source" -type f -print0 | sort -z)
}
copy_text fresh-final-host /home/sean/cdk2-fresh-admission-final-refreeze.3ek53p
copy_text public-goal-host /home/sean/fresh-renderer-public-goal-host.UXyCdb
copy_text joined-host /home/sean/joined-runtime-capture-tpm-ready652.ASlZmW
copy_text failed-build650 /home/sean/fresh-normal-after650.3DTUmz
copy_text failed-build652 /home/sean/fresh-normal-after652.JeGj3p
# Only explicit actual build text receipts: no executable, firmware, imported
# source tree or learned guest media is included in this evidence packet.
for name in build/build.log build/build.time build/outer.status build/configure.log build/configure.time build/direct-provenance.log build/source-before.sha256 build/source-after-check.log build/inputs-before.sha256 build/inputs-after-check.log build/outputs.sha256 build/initial9/configure.log build/initial9/source-before.sha256 build/initial9/configure.argv.json build/initial9/build.argv.json; do
	source=/home/sean/fresh-normal-after652.JeGj3p/$name
	test -f "$source" && test ! -L "$source"
	mkdir -p "$destination/failed-build652/$(dirname "$name")"
	cp -p "$source" "$destination/failed-build652/$name"
	cmp "$source" "$destination/failed-build652/$name"
	sha256sum "$source" >> "$destination/ORIGINAL_FILES.sha256"
done
cp -p "$0" "$destination/collector.sh"
(cd "$destination" && find . -type f ! -name ARCHIVE.sha256 -print0 | sort -z | xargs -0 sha256sum > ARCHIVE.sha256)
(cd "$destination" && sha256sum -c ARCHIVE.sha256)
