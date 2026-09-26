# SPDX-License-Identifier: GPL-2.0-only

function fail_format()
{
	print "unparsed checkpatch diagnostic near input line " NR > "/dev/stderr"
	exit 2
}

/^(WARNING|ERROR|CHECK):[A-Z_]+:/ {
	if (pending)
		fail_format()
	pending = 1
	severity = $1
	sub(/:.*/, "", severity)
	split($0, part, ":")
	type = part[2]
	message = $0
	sub(/^[^:]+:[^:]+:[[:space:]]*/, "", message)
	if (getline <= 0 || $0 !~ /^#[0-9]+: FILE: /)
		fail_format()
	fileline = $0
	sub(/^#[0-9]+: FILE: /, "", fileline)
	sub(/:$/, "", fileline)
	while (getline > 0) {
		if ($0 ~ /^(WARNING|ERROR|CHECK):[A-Z_]+:/)
			fail_format()
		if ($0 ~ /^\+/) {
			anchor = $0
			sub(/^\+/, "", anchor)
			print severity "|" type "|" fileline "|" message "|" anchor
			pending = 0
			break
		}
	}
	if (pending)
		fail_format()
}

END {
	if (pending)
		fail_format()
}
