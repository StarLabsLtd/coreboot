#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

usage()
{
	echo "usage: $0 <manifest> <coreboot-tree> <resolved-config> <enabled-sources> <preprocessed-list> <generated-devicetree>" >&2
	exit 2
}

[ "$#" -eq 6 ] || usage
manifest=$1
tree=$2
config=$3
sources=$4
preprocessed=$5
devicetree=$6

for input in "$manifest" "$config" "$sources" "$preprocessed" "$devicetree"; do
	[ -f "$input" ] || { echo "missing audit input: $input" >&2; exit 1; }
done

case "$tree" in
	/*) ;;
	*) echo "coreboot tree must be absolute" >&2; exit 1 ;;
esac

tmp_base=${TMPDIR:-/tmp}
tmp=$(mktemp -d "$tmp_base/cb-busmaster-check.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
: > "$tmp/expected-config"
: > "$tmp/expected-sites"
: > "$tmp/expected-expanded"
: > "$tmp/expected-devicetree"
: > "$tmp/manifest-sources"

awk -F '\t' '
/^[[:space:]]*#/ || NF == 0 { next }
$1 == "config" {
	if (NF != 4 || $2 !~ /^CONFIG_[A-Z0-9_]+$/ || $3 !~ /^(y|n)$/ || $4 == "")
		bad("malformed config row", NR)
	if (configs[$2]++) bad("duplicate config row", NR)
	print $2 "\t" $3 > config_out
	next
}
$1 == "site" {
	if (NF != 7 || $2 !~ /^(enable|disable|dormant|capability)$/ ||
	    $3 !~ /^(coreboot|payload)$/ || $4 == "" || $5 == "" ||
	    $6 !~ /^[0-9a-f]{64}$/ || $7 == "")
		bad("malformed site row", NR)
	if ($5 ~ /^\// || $5 ~ /(^|\/)\.\.($|\/)/)
		bad("unsafe source path", NR)
	key = $2 "\t" $3 "\t" $4 "\t" $5 "\t" $6
	if (sites[key]++) bad("duplicate site row", NR)
	print key > site_out
	print $5 > source_out
	next
}
$1 == "expanded" {
	if (NF != 8 || $2 !~ /^(enable|disable|dormant|unknown)$/ ||
	    $3 !~ /^(coreboot|payload)$/ ||
	    $4 !~ /^(command-register|command-overlap|unknown-overlap)$/ ||
	    $5 !~ /^(bootblock|romstage|postcar|ramstage|smm|verstage)$/ ||
	    $6 == "" || $7 !~ /^[0-9a-f]{64}$/ || $8 == "")
		bad("malformed expanded row", NR)
	if ($6 ~ /^\// || $6 ~ /(^|\/)\.\.($|\/)/)
		bad("unsafe expanded source path", NR)
	key = $2 "\t" $3 "\t" $4 "\t" $5 "\t" $6 "\t" $7
	if (expanded[key]++) bad("duplicate expanded row", NR)
	print key > expanded_out
	next
}
$1 == "devicetree" {
	if (NF != 3 || $2 !~ /^[0-9a-f]{64}$/ || $3 == "" || devicetree_seen++)
		bad("malformed devicetree row", NR)
	print $2 > devicetree_out
	next
}
{ bad("unknown manifest row", NR) }
function bad(message, line) {
	print message " at manifest line " line > "/dev/stderr"
	failed = 1
}
END { exit failed }
' config_out="$tmp/expected-config" site_out="$tmp/expected-sites" \
	expanded_out="$tmp/expected-expanded" \
	devicetree_out="$tmp/expected-devicetree" \
	source_out="$tmp/manifest-sources" "$manifest"

[ "$(wc -l < "$tmp/expected-devicetree")" -eq 1 ] || {
	echo "missing generated devicetree evidence" >&2
	exit 1
}
actual_devicetree=$(sha256sum "$devicetree" | awk '{print $1}')
expected_devicetree=$(cat "$tmp/expected-devicetree")
[ "$actual_devicetree" = "$expected_devicetree" ] || {
	echo "generated devicetree drift: expected $expected_devicetree, got $actual_devicetree" >&2
	exit 1
}

for required in CONFIG_PCI_ALLOW_BUS_MASTER_ANY_DEVICE \
	CONFIG_PCI_SET_BUS_MASTER_PCI_BRIDGES CONFIG_PAYLOAD_OWNS_PCI_DEVICES \
	CONFIG_RUN_FSP_GOP CONFIG_NO_GFX_INIT CONFIG_TCG_OPAL_S3_UNLOCK \
	CONFIG_CONSOLE_SERIAL; do
	grep -q "^${required}$(printf '\t')" "$tmp/expected-config" || {
		echo "missing required policy input: $required" >&2
		exit 1
	}
done

while IFS="$(printf '\t')" read -r symbol expected; do
	case "$expected" in
	y) grep -qx "$symbol=y" "$config" || {
		echo "resolved config drift: expected $symbol=y" >&2; exit 1; } ;;
	n) grep -qx "# $symbol is not set" "$config" || {
		echo "resolved config drift: expected $symbol=n" >&2; exit 1; } ;;
	esac
done < "$tmp/expected-config"

sort -u "$sources" > "$tmp/sources"
[ -s "$tmp/sources" ] || { echo "empty enabled-source graph" >&2; exit 1; }

while IFS= read -r source; do
	case "$source" in
	""|..|/*|*../*|../*|*/..) echo "unsafe enabled source path: $source" >&2; exit 1 ;;
	esac
	[ -f "$tree/$source" ] || {
		echo "stale enabled source: $source" >&2; exit 1;
	}
done < "$tmp/sources"

sort -u "$tmp/manifest-sources" | while IFS= read -r source; do
	grep -Fxq "$source" "$tmp/sources" || {
		echo "manifest source is not in resolved build graph: $source" >&2
		exit 1
	}
done

: > "$tmp/actual-sites"
while IFS= read -r source; do
	contexts="$tmp/site-contexts"
	if ! awk '
	{
		text[NR] = $0
		semantic[NR] = semantic_text($0)
	}
	function semantic_text(source, output, cursor, character, next_character) {
		output = ""
		for (cursor = 1; cursor <= length(source); cursor++) {
			character = substr(source, cursor, 1)
			next_character = substr(source, cursor + 1, 1)
			if (block_comment) {
				if (character == "*" && next_character == "/") {
					output = output "  "
					cursor++
					block_comment = 0
				} else output = output " "
				continue
			}
			if (string_quote != "") {
				output = output " "
				if (escaped) escaped = 0
				else if (character == "\\") escaped = 1
				else if (character == string_quote) string_quote = ""
				continue
			}
			if (character == "/" && next_character == "*") {
				output = output "  "
				cursor++
				block_comment = 1
				continue
			}
			if (character == "/" && next_character == "/")
				return output
			if (character == "\"" || character == "\047") {
				output = output " "
				string_quote = character
				continue
			}
			output = output character
		}
		return output
	}
	function statement_prefix(line, prefix, position, part) {
		prefix = ""
		for (position = line - 1; position > 0; position--) {
			part = semantic[position]
			if (part ~ /[;{}]/) {
				sub(/^.*[;{}]/, "", part)
				return part " " prefix
			}
			prefix = part " " prefix
		}
		return prefix
	}
	function following_semantic(source, line, position) {
		position = line + 1
		while (source ~ /^[[:space:]]*$/ && position <= NR) {
			source = source " " semantic[position]
			position++
		}
		return source
	}
	function helper_reference(source, helper, declaration_prefix, line,
				  rest, before, after, following, prefix, suffix) {
		rest = source
		while (match(rest, helper)) {
			before = substr(rest, 1, RSTART - 1)
			after = substr(rest, RSTART + RLENGTH)
			following = following_semantic(after, line)
			prefix = substr(before, length(before), 1)
			suffix = substr(after, 1, 1)
			if ((prefix == "" || prefix !~ /[A-Za-z0-9_]/) &&
			    (suffix == "" || suffix !~ /[A-Za-z0-9_]/) &&
			    !(following ~ /^[[:space:]]*\(/ &&
			      helper_declarator(declaration_prefix before)))
				return 1
			rest = after
		}
		return 0
	}
	function helper_declarator(before, declaration) {
		declaration = before
		sub(/^.*[;{}]/, "", declaration)
		gsub(/__attribute__[[:space:]]*\(\([^)]*\)\)[[:space:]]*/, "", declaration)
		return declaration ~ /^[[:space:]]*((extern|static|inline|__always_inline|__weak)[[:space:]]+)*void[[:space:]]+([A-Za-z_][A-Za-z0-9_]*[[:space:]]+)*$/
	}
	END {
		for (line = 1; line <= NR; line++) {
			if (semantic[line] !~ /pci_dev_(request|disable)_bus_master|PCI_COMMAND_MASTER/)
				continue
			context = ""
			first = line > 2 ? line - 2 : 1
			last = line + 2 < NR ? line + 2 : NR
			for (position = first; position <= last; position++) {
				part = semantic[position]
				gsub(/^[[:space:]]+|[[:space:]]+$/, "", part)
				gsub(/[[:space:]]+/, " ", part)
				if (part != "")
					context = context (context == "" ? "" : " ") part
			}
			prefix = statement_prefix(line)
			request = helper_reference(semantic[line],
				"pci_dev_request_bus_master", prefix, line)
			disable = helper_reference(semantic[line],
				"pci_dev_disable_bus_master", prefix, line)
			if (!request && !disable &&
			    semantic[line] ~ /pci_dev_(request|disable)_bus_master/ &&
			    semantic[line] !~ /PCI_COMMAND_MASTER/)
				continue
			if (!request && !disable &&
			    context !~ /pci_[a-z_]*config(16|32).*PCI_COMMAND_MASTER/ &&
				   context !~ /dev->command.*PCI_COMMAND_MASTER/ &&
				   context !~ /(reg16|pcireg).*PCI_COMMAND_MASTER/ &&
				   context !~ /#define UART_PCI_ENABLE.*PCI_COMMAND_MASTER/) {
				print "unrecognized PCI_COMMAND_MASTER operation at " FILENAME ":" line > "/dev/stderr"
				failed = 1
				continue
			}
			operation = "command"
			if (request) operation = "request"
			if (disable) operation = "disable"
			print line "\t" operation "\t" context
		}
		exit failed
	}' "$tree/$source" > "$contexts"; then
		exit 1
	fi
	while IFS="$(printf '\t')" read -r source_line operation context; do
		state=enable
		[ "$operation" != disable ] || state=disable
		owner=coreboot
		case "$context" in
		*'~PCI_COMMAND_MASTER'*|*'~('*'PCI_COMMAND_MASTER'*) state=disable ;;
		esac
		case "$source" in
		src/soc/intel/common/block/dsp/dsp.c)
			if [ "$state" = disable ]; then class=dsp-finalize
			else state=dormant; owner=payload; class=dsp-endpoint; fi ;;
		src/soc/intel/common/block/hda/hda.c)
			if [ "$state" = disable ]; then class=hda-finalize
			else state=dormant; owner=payload; class=hda-endpoint; fi ;;
		src/soc/intel/common/block/sata/sata.c)
			if [ "$state" = disable ]; then class=sata-finalize
			else state=dormant; owner=payload; class=sata-endpoint; fi ;;
		src/soc/intel/common/block/gspi/gspi.c)
			state=dormant
			if [ "$source_line" -lt 150 ]; then class=gspi-early; else class=gspi-runtime; fi ;;
		src/soc/intel/common/block/i2c/i2c.c)
			if [ "$state" = disable ]; then class=i2c-finalize
			else state=dormant; class=i2c-early; fi ;;
		src/soc/intel/common/block/xhci/xhci.c) class=xhci-finalize ;;
		src/soc/intel/common/feature/finalize/finalize.c) class=thunderbolt-finalize ;;
		src/soc/intel/common/block/uart/uart.c) state=capability; class=uart-controller ;;
		src/device/pci_device.c)
			if [ "$state" = disable ]; then class=endpoint-helper
			elif printf '%s\n' "$context" | grep -q PCI_BRIDGE; then class=bridge-resource
			else class=system-class; fi ;;
		src/security/tcg/opal_s3/opal_nvme.c)
			if [ "$state" = disable ]; then class=opal-s3-error; else class=opal-s3-nvme; fi ;;
		src/soc/intel/common/block/cse/cse.c)
			if [ "$source_line" -lt 500 ]; then class=cse-early; else class=cse-state; fi ;;
		src/soc/intel/common/block/fast_spi/fast_spi.c) class=fast-spi-early ;;
		src/soc/intel/common/block/graphics/graphics.c) class=graphics-endpoint ;;
		src/soc/intel/common/block/p2sb/p2sblib.c) class=p2sb-platform ;;
		src/soc/intel/common/block/pcie/pcie.c) class=pcie-bridge ;;
		src/soc/intel/common/block/smm/smihandler.c) class=smm-sweep ;;
		src/soc/intel/meteorlake/bootblock/soc_die.c) class=pmc-bootblock ;;
		src/owner.c) class=fixture ;;
		*) echo "unclassified raw bus-master site: $source:$source_line" >&2; exit 1 ;;
		esac
		digest=$(printf '%s\n%s\n' "$source" "$context" | sha256sum | awk '{print $1}')
		printf '%s\t%s\t%s\t%s\t%s\n' "$state" "$owner" "$class" "$source" "$digest" \
			>> "$tmp/actual-sites"
	done < "$contexts"
done < "$tmp/sources"

sort "$tmp/expected-sites" > "$tmp/expected-sites.sorted"
sort "$tmp/actual-sites" > "$tmp/actual-sites.sorted"
if ! cmp -s "$tmp/expected-sites.sorted" "$tmp/actual-sites.sorted"; then
	echo "PCI bus-master site inventory drift:" >&2
	diff -u "$tmp/expected-sites.sorted" "$tmp/actual-sites.sorted" >&2 || true
	exit 1
fi

: > "$tmp/expanded-statements.unsorted"
while IFS="$(printf '\t')" read -r stage unit; do
	case "$stage" in
	bootblock|romstage|postcar|ramstage|smm|verstage) ;;
	*) echo "invalid preprocessed stage: $stage" >&2; exit 1 ;;
	esac
	[ -f "$unit" ] || { echo "stale preprocessed unit: $unit" >&2; exit 1; }
	if ! awk '
	/^#[[:space:]]+[0-9]+[[:space:]]+"/ { source_line = $2; source_file = $3; next }
	{
		count = split($0, pieces, /;/)
		for (piece = 1; piece <= count; piece++) {
			print "# " source_line " " source_file
			print pieces[piece] (piece < count ? ";" : "")
		}
		source_line++
	}' "$unit" > "$tmp/preprocessed-statements"; then
		exit 1
	fi
	if ! awk -v root="$tree/" -v stage="$stage" '
	/^#[[:space:]]+[0-9]+[[:space:]]+"/ {
		line = $2
		file = $3
		gsub(/^"|"$/, "", file)
		if (index(file, root) == 1)
			file = substr(file, length(root) + 1)
		else if (match(file, /\/build\//))
			file = "generated/" substr(file, RSTART + 7)
		next
	}
	{
		if (statement == "") {
			origin = file
			origin_line = line
		}
		line++
		part = $0
		gsub(/^[[:space:]]+|[[:space:]]+$/, "", part)
		gsub(/[[:space:]]+/, " ", part)
		statement = statement (statement == "" ? "" : " ") part
		if ($0 !~ /;/) next
		record_enum_constants(statement)
		mutator = statement ~ /pci_[a-z_]*(or|and|write|update)_config(8|16|32)[[:space:]]*\(/
		width = mutator ? access_width(statement) : 0
		offset = mutator ? resolved_offset(argument(statement, 2)) : -1
		offset_kind = mutator ? classify_offset(offset, width) : ""
		request_helper = helper_invocation(statement, "pci_dev_request_bus_master")
		disable_helper = helper_invocation(statement, "pci_dev_disable_bus_master")
		if ((mutator && offset_kind != "other") || request_helper || disable_helper ||
		    statement ~ /->[[:space:]]*command[[:space:]]*[|&^]?=/) {
			state = "unknown"
			class = "command-register"
			if (offset_kind == "unknown") class = "unknown-overlap"
			else if (offset_kind == "overlap") class = "command-overlap"
			bme_bit = offset_kind == "unknown" ? -1 : bme_access_bit(offset, width)
			if (request_helper ||
			    (bme_bit >= 0 && statement ~ /pci_[a-z_]*or_config(8|16|32)/ &&
			     has_bit(argument(statement, 3), bme_bit)) ||
			    (bme_bit >= 0 && statement ~ /pci_[a-z_]*(write|update)_config(8|16|32)/ &&
			     has_bit(argument(statement, statement ~ /update_config/ ? 4 : 3), bme_bit)) ||
			    statement ~ /->[[:space:]]*command[[:space:]]*\|=.*([ (]|^)(4|0x0*4)([ );]|$)/)
				state = "enable"
			else if (disable_helper)
				state = "disable"
			else if ((bme_bit >= 0 && statement ~ /pci_[a-z_]*and_config(8|16|32)/ &&
				  argument(statement, 3) ~ /~/ && has_bit(argument(statement, 3), bme_bit)) ||
				 (bme_bit >= 0 && statement ~ /pci_[a-z_]*update_config(8|16|32)/ &&
				  argument(statement, 3) ~ /~/ &&
				  has_bit(argument(statement, 3), bme_bit) &&
				  bit_is_known_clear(argument(statement, 4), bme_bit)) ||
				 (bme_bit >= 0 && statement ~ /pci_[a-z_]*write_config(8|16|32)/ &&
				  bit_is_known_clear(argument(statement, 3), bme_bit)) ||
				 statement ~ /->[[:space:]]*command[[:space:]]*&=[[:space:]]*~.*(4|0x0*4)/)
				state = "disable"
			owner = "coreboot"
			if (origin ~ /src\/soc\/intel\/common\/block\/(hda|dsp|sata)\// &&
			    statement ~ /pci_dev_request_bus_master/) {
				owner = "payload"
				state = "dormant"
			}
			print state "\t" owner "\t" class "\t" stage "\t" origin "\t" origin_line "\t" statement
		}
		statement = ""
	}
	function helper_invocation(source, helper, rest, before) {
		rest = source
		while (match(rest, helper "[[:space:]]*\\(")) {
			before = substr(rest, 1, RSTART - 1)
			if (!helper_declarator(before))
				return 1
			rest = substr(rest, RSTART + RLENGTH)
		}
		return 0
	}
	function helper_declarator(before, declaration) {
		declaration = before
		sub(/^.*[;{}]/, "", declaration)
		gsub(/__attribute__[[:space:]]*\(\([^)]*\)\)[[:space:]]*/, "", declaration)
		return declaration ~ /^[[:space:]]*((extern|static|inline|__always_inline|__weak)[[:space:]]+)*void[[:space:]]+([A-Za-z_][A-Za-z0-9_]*[[:space:]]+)*$/
	}
	function argument(text, wanted, start, position, depth, commas, character) {
		match(text, /pci_[a-z_]*(or|and|write|update)_config(8|16|32)[[:space:]]*\(/)
		if (!RSTART) return ""
		start = RSTART + RLENGTH
		depth = 0
		commas = 0
		for (position = start; position <= length(text); position++) {
			character = substr(text, position, 1)
			if (character == "(") depth++
			else if (character == ")") {
				if (depth == 0) return wanted == commas + 1 ? substr(text, start, position - start) : ""
				depth--
			} else if (character == "," && depth == 0) {
				commas++
				if (wanted == commas) return substr(text, start, position - start)
				start = position + 1
			}
		}
		return ""
	}
	function clean(expression) {
		gsub(/[[:space:]()]/, "", expression)
		return expression
	}
	function atom_value(atom, value, digit, shift, parts, left, right) {
		shift = split(atom, parts, /<</)
		if (shift == 2) {
			left = atom_value(parts[1])
			right = atom_value(parts[2])
			if (left < 0 || right < 0 || right >= 63) return -1
			return left * (2 ^ right)
		}
		if (shift != 1) return -1
		sub(/[uUlL]+$/, "", atom)
		if (atom ~ /^0[xX][0-9a-fA-F]+$/) {
			sub(/^0[xX]/, "", atom)
			value = 0
			while (length(atom)) {
				digit = index("0123456789abcdef", tolower(substr(atom, 1, 1))) - 1
				value = value * 16 + digit
				atom = substr(atom, 2)
			}
			return value
		}
		return atom ~ /^[0-9]+$/ ? atom + 0 : -1
	}
	function constant_value(expression, terms, count, left, right) {
		expression = clean(expression)
		count = split(expression, terms, /\+/)
		if (count == 1) return atom_value(terms[1])
		if (count == 2) {
			left = atom_value(terms[1]); right = atom_value(terms[2])
			return left >= 0 && right >= 0 ? left + right : -1
		}
		return -1
	}
	function classify_offset(value, width, last_byte) {
		if (value < 0) return "unknown"
		last_byte = value + width / 8 - 1
		if (last_byte < 4 || value > 5) return "other"
		if (value == 4) return "command"
		return "overlap"
	}
	function access_width(text, matched) {
		match(text, /config(8|16|32)/)
		matched = substr(text, RSTART + 6, RLENGTH - 6)
		return matched + 0
	}
	function bme_access_bit(offset, width) {
		if (offset > 4 || offset + width / 8 <= 4) return -1
		return (4 - offset) * 8 + 2
	}
	function resolved_offset(expression, value, symbol) {
		value = constant_value(expression)
		symbol = clean(expression)
		if (value < 0 && symbol in enum_value) value = enum_value[symbol]
		return value
	}
	function has_bit(expression, bit, normalized, value, divisor, terms, count, index_) {
		normalized = clean(expression)
		divisor = 2 ^ bit
		gsub(/~/, "", normalized)
		count = split(normalized, terms, /\|/)
		for (index_ = 1; index_ <= count; index_++) {
			value = constant_value(terms[index_])
			if (value >= 0 && int(value / divisor) % 2) return 1
		}
		return 0
	}
	function bit_is_known_clear(expression, bit, normalized, terms, count, index_, value, divisor) {
		normalized = clean(expression)
		if (normalized ~ /~/) return 0
		divisor = 2 ^ bit
		count = split(normalized, terms, /\|/)
		for (index_ = 1; index_ <= count; index_++) {
			value = constant_value(terms[index_])
			if (value < 0 || int(value / divisor) % 2) return 0
		}
		return 1
	}
	function record_enum_constants(text, body, entries, count, index_, equals, name, expression, value) {
		if (text !~ /enum[^{]*\{[^}]*\}/) return
		body = text
		sub(/^[^{]*\{/, "", body)
		sub(/\}.*/, "", body)
		count = split(body, entries, /,/)
		for (index_ = 1; index_ <= count; index_++) {
			equals = index(entries[index_], "=")
			if (!equals) continue
			name = substr(entries[index_], 1, equals - 1)
			expression = substr(entries[index_], equals + 1)
			gsub(/[[:space:]]/, "", name)
			if (name !~ /^[A-Za-z_][A-Za-z0-9_]*$/) continue
			value = constant_value(expression)
			if (value >= 0) enum_value[name] = value
		}
	}
	' "$tmp/preprocessed-statements" >> "$tmp/expanded-statements.unsorted"; then
		exit 1
	fi
done < "$preprocessed"
sort -u "$tmp/expanded-statements.unsorted" > "$tmp/expanded-statements"

while IFS="$(printf '\t')" read -r state owner class stage source source_line statement; do
	case "$source" in
	""|/*|..|../*|*../*|*/..) continue ;;
	esac
	digest=$(printf '%s:%s\n%s\n' "$source" "$source_line" "$statement" |
		sha256sum | awk '{print $1}')
	printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$state" "$owner" "$class" "$stage" "$source" "$digest" \
		>> "$tmp/actual-expanded"
done < "$tmp/expanded-statements"

sort "$tmp/expected-expanded" > "$tmp/expected-expanded.sorted"
sort -u "$tmp/actual-expanded" > "$tmp/actual-expanded.sorted"
if ! cmp -s "$tmp/expected-expanded.sorted" "$tmp/actual-expanded.sorted"; then
	echo "preprocessed PCI command mutation inventory drift:" >&2
	diff -u "$tmp/expected-expanded.sorted" "$tmp/actual-expanded.sorted" >&2 || true
	exit 1
fi

echo "StarBook MTL PCI bus-master ownership audit passed"
