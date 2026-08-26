#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
checker=$root/util/check-pci-busmaster-ownership.sh
tmp_base=${TMPDIR:-/tmp}
tmp=$(mktemp -d "$tmp_base/cb-busmaster-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
tab=$(printf '\t')

"$root/util/audit-starbook-mtl-pci-busmaster.sh"

expect_failure()
{
	name=$1
	shift
	if "$@" > "$tmp/$name.out" 2>&1; then
		echo "mutation unexpectedly passed: $name" >&2
		exit 1
	fi
	case "$name" in
wrong_state|wrong_owner|wrong_stage|stage_removed|numeric_direct|config8_command|\
overlap16_at3|overlap32_at1|overlap32_at2|overlap32_at3|overlap32_at4|\
overlap_byte5|offset_sum|command_plus_zero|enum_offset|variable_offset|\
same_line_offset|header_helper|generated_include|disable_helper|inline_helper_body|\
macro_helper|return_helper|comma_helper|ternary_helper|cast_helper|paren_helper|\
update_disable|shift_20|\
write_zero|write_clear|write_enable|write_unknown|write_overlap_clear|\
write_offset_unknown)
		expected='preprocessed PCI command mutation inventory drift' ;;
	wrong_site_state|wrong_site_owner|wrong_site_class)
		expected='PCI bus-master site inventory drift' ;;
	wrong_class) expected='malformed expanded row' ;;
	config_branch) expected='resolved config drift' ;;
	path_escape) expected='unsafe source path' ;;
	stale) expected='stale enabled source|manifest source is not in resolved build graph' ;;
	duplicate) expected='duplicate site row' ;;
	prose_evidence|missing|comment|new_site|callback_missing|raw_disable_helper|\
	raw_inline_helper_body|raw_macro_helper|raw_split_line_helper|\
	raw_return_helper|raw_comma_helper|raw_ternary_helper|raw_cast_helper|raw_paren_helper)
		expected='PCI bus-master site inventory drift' ;;
	stale_preprocessed) expected='stale preprocessed unit' ;;
	devicetree_drift) expected='generated devicetree drift' ;;
	*) echo "mutation has no diagnostic expectation: $name" >&2; exit 1 ;;
	esac
	grep -Eq "$expected" "$tmp/$name.out" || {
		echo "mutation failed for the wrong reason: $name" >&2
		cat "$tmp/$name.out" >&2
		exit 1
	}
}

mkdir -p "$tmp/tree/src"
cat > "$tmp/tree/src/owner.c" <<'EOF'
void enable_owner(void)
{
	pci_or_config16(dev, PCI_COMMAND, PCI_COMMAND_MASTER);
}
EOF
cat > "$tmp/config" <<'EOF'
CONFIG_PCI_ALLOW_BUS_MASTER_ANY_DEVICE=y
CONFIG_PCI_SET_BUS_MASTER_PCI_BRIDGES=y
CONFIG_PAYLOAD_OWNS_PCI_DEVICES=y
CONFIG_RUN_FSP_GOP=y
# CONFIG_NO_GFX_INIT is not set
CONFIG_TCG_OPAL_S3_UNLOCK=y
# CONFIG_CONSOLE_SERIAL is not set
EOF
printf 'src/owner.c\n' > "$tmp/sources"
cat > "$tmp/owner.i" <<EOF
# 1 "$tmp/tree/src/owner.c"
void enable_owner(void)
{
 pci_or_config16(dev, 4, (1 << 2));
}
EOF
printf 'ramstage\t%s\n' "$tmp/owner.i" > "$tmp/preprocessed-list"
printf '%s\n' 'generated static devicetree fixture' > "$tmp/static.c"

context='void enable_owner(void) { pci_or_config16(dev, PCI_COMMAND, PCI_COMMAND_MASTER); }'
digest=$(printf '%s\n%s\n' src/owner.c "$context" | sha256sum | awk '{print $1}')
expanded_statement='void enable_owner(void) { pci_or_config16(dev, 4, (1 << 2));'
expanded_digest=$(printf '%s:%s\n%s\n' src/owner.c 1 "$expanded_statement" |
	sha256sum | awk '{print $1}')
devicetree_digest=$(sha256sum "$tmp/static.c" | awk '{print $1}')
cat > "$tmp/manifest" <<EOF
# SPDX-License-Identifier: GPL-2.0-only
devicetree${tab}${devicetree_digest}${tab}generated devicetree fixture
config${tab}CONFIG_PCI_ALLOW_BUS_MASTER_ANY_DEVICE${tab}y${tab}fixture
config${tab}CONFIG_PCI_SET_BUS_MASTER_PCI_BRIDGES${tab}y${tab}fixture
config${tab}CONFIG_PAYLOAD_OWNS_PCI_DEVICES${tab}y${tab}fixture
config${tab}CONFIG_RUN_FSP_GOP${tab}y${tab}fixture
config${tab}CONFIG_NO_GFX_INIT${tab}n${tab}fixture
config${tab}CONFIG_TCG_OPAL_S3_UNLOCK${tab}y${tab}fixture
config${tab}CONFIG_CONSOLE_SERIAL${tab}n${tab}fixture
site${tab}enable${tab}coreboot${tab}fixture${tab}src/owner.c${tab}${digest}${tab}fixture owner
expanded${tab}enable${tab}coreboot${tab}command-register${tab}ramstage${tab}src/owner.c${tab}${expanded_digest}${tab}fixture expansion
EOF

check_fixture()
{
	"$checker" "$@" "$tmp/tree" "$tmp/config" "$tmp/sources" \
		"$tmp/preprocessed-list" "$tmp/static.c"
}

check_fixture "$tmp/manifest"

# Helper declarations and definition signatures describe types/ownership but
# execute no PCI mutation. Calls in ordinary bodies, inline bodies, and macro
# expansions remain part of both inventories.
cp "$tmp/owner.i" "$tmp/declaration-base.i"
cat >> "$tmp/owner.i" <<'EOF'
void pci_dev_disable_bus_master(struct device *dev);
void pci_dev_request_bus_master(struct device *dev);
void pci_dev_disable_bus_master(struct device *dev) { }
EOF
check_fixture "$tmp/manifest" > /dev/null
cp "$tmp/declaration-base.i" "$tmp/owner.i"
cat > "$tmp/tree/src/declarations.c" <<'EOF'
void pci_dev_disable_bus_master(struct device *dev);
void pci_dev_request_bus_master(struct device *dev);
void pci_dev_disable_bus_master(struct device *dev) { }
void
pci_dev_request_bus_master(struct device *dev);
extern void pci_dev_request_bus_master
/* The opening parenthesis may begin after comment-only lines. */
(struct device *dev);
static void
pci_dev_disable_bus_master
/* Likewise for a multiline definition signature. */
(
	struct device *dev
)
{
}
EOF
printf 'src/owner.c\nsrc/declarations.c\n' > "$tmp/sources"
check_fixture "$tmp/manifest" > /dev/null
printf 'src/owner.c\n' > "$tmp/sources"

sed '/^site/ s/enable/disable/' "$tmp/manifest" > "$tmp/wrong-site-state"
expect_failure wrong_site_state check_fixture "$tmp/wrong-site-state"
sed '/^site/ s/coreboot/payload/' "$tmp/manifest" > "$tmp/wrong-site-owner"
expect_failure wrong_site_owner check_fixture "$tmp/wrong-site-owner"
sed '/^site/ s/fixture/other-class/' "$tmp/manifest" > "$tmp/wrong-site-class"
expect_failure wrong_site_class check_fixture "$tmp/wrong-site-class"

sed '/^expanded/ s/enable/disable/' "$tmp/manifest" \
	> "$tmp/wrong-state"
expect_failure wrong_state check_fixture "$tmp/wrong-state"
sed '/^expanded/ s/coreboot/payload/' "$tmp/manifest" \
	> "$tmp/wrong-owner"
expect_failure wrong_owner check_fixture "$tmp/wrong-owner"
sed '/^expanded/ s/ramstage/romstage/' "$tmp/manifest" > "$tmp/wrong-stage"
expect_failure wrong_stage check_fixture "$tmp/wrong-stage"
cp "$tmp/manifest" "$tmp/multistage-manifest"
sed -n '/^expanded/ s/ramstage/verstage/p' "$tmp/manifest" \
	>> "$tmp/multistage-manifest"
printf 'ramstage\t%s\nverstage\t%s\n' "$tmp/owner.i" "$tmp/owner.i" \
	> "$tmp/preprocessed-list"
check_fixture "$tmp/multistage-manifest"
printf 'ramstage\t%s\n' "$tmp/owner.i" > "$tmp/preprocessed-list"
expect_failure stage_removed check_fixture "$tmp/multistage-manifest"
sed '/^expanded/ s/command-register/endpoint/' \
	"$tmp/manifest" > "$tmp/wrong-class"
expect_failure wrong_class check_fixture "$tmp/wrong-class"

cp "$tmp/owner.i" "$tmp/original-owner.i"
cat >> "$tmp/owner.i" <<'EOF'
pci_write_config16(dev, 0x04, 0x4);
EOF
expect_failure numeric_direct check_fixture "$tmp/manifest"
cp "$tmp/original-owner.i" "$tmp/owner.i"

cat >> "$tmp/owner.i" <<'EOF'
pci_write_config16(dev, 4, 0);
EOF
expect_failure write_zero check_fixture "$tmp/manifest"
grep -q '^+disable.*command-register' "$tmp/write_zero.out" || {
	echo "constant zero write was not classified as disable" >&2; exit 1;
}
cp "$tmp/original-owner.i" "$tmp/owner.i"

cat >> "$tmp/owner.i" <<'EOF'
pci_write_config16(dev, 4, 0x1);
EOF
expect_failure write_clear check_fixture "$tmp/manifest"
grep -q '^+disable.*command-register' "$tmp/write_clear.out" || {
	echo "constant write clearing BME was not classified as disable" >&2; exit 1;
}
cp "$tmp/original-owner.i" "$tmp/owner.i"

cat >> "$tmp/owner.i" <<'EOF'
pci_write_config16(dev, 4, 0x4);
EOF
expect_failure write_enable check_fixture "$tmp/manifest"
grep -q '^+enable.*command-register' "$tmp/write_enable.out" || {
	echo "constant write setting BME was not classified as enable" >&2; exit 1;
}
cp "$tmp/original-owner.i" "$tmp/owner.i"

cat >> "$tmp/owner.i" <<'EOF'
pci_write_config16(dev, 4, value);
EOF
expect_failure write_unknown check_fixture "$tmp/manifest"
grep -q '^+unknown.*command-register' "$tmp/write_unknown.out" || {
	echo "unknown write value was not classified conservatively" >&2; exit 1;
}
cp "$tmp/original-owner.i" "$tmp/owner.i"

cat >> "$tmp/owner.i" <<'EOF'
pci_write_config32(dev, 3, 0x1);
EOF
expect_failure write_overlap_clear check_fixture "$tmp/manifest"
grep -q '^+disable.*command-overlap' "$tmp/write_overlap_clear.out" || {
	echo "mixed-width constant clear was not classified as disable" >&2; exit 1;
}
cp "$tmp/original-owner.i" "$tmp/owner.i"

cat >> "$tmp/owner.i" <<'EOF'
pci_write_config16(dev, offset, 0);
EOF
expect_failure write_offset_unknown check_fixture "$tmp/manifest"
grep -q '^+unknown.*unknown-overlap' "$tmp/write_offset_unknown.out" || {
	echo "unknown write offset was not classified conservatively" >&2; exit 1;
}
cp "$tmp/original-owner.i" "$tmp/owner.i"

# A constant write outside command-register bytes is not a BME mutation.
cat >> "$tmp/owner.i" <<'EOF'
pci_write_config16(dev, 6, 0);
EOF
check_fixture "$tmp/manifest" > /dev/null
cp "$tmp/original-owner.i" "$tmp/owner.i"

cat >> "$tmp/owner.i" <<'EOF'
pci_update_config16(dev, 4, ~(1U << 2), 0);
EOF
expect_failure update_disable check_fixture "$tmp/manifest"
grep -q '^+disable.*command-register' "$tmp/update_disable.out" || {
	echo "update mask clearing BME was not classified as disable" >&2; exit 1;
}
cp "$tmp/original-owner.i" "$tmp/owner.i"

cat >> "$tmp/owner.i" <<'EOF'
pci_or_config32(dev, 4, (1U << 20));
EOF
expect_failure shift_20 check_fixture "$tmp/manifest"
grep -q '^+unknown.*command-register' "$tmp/shift_20.out" || {
	echo "shift count 20 was confused with BME bit 2" >&2; exit 1;
}
cp "$tmp/original-owner.i" "$tmp/owner.i"

cat >> "$tmp/owner.i" <<'EOF'
pci_dev_disable_bus_master(dev);
EOF
expect_failure disable_helper check_fixture "$tmp/manifest"
cp "$tmp/original-owner.i" "$tmp/owner.i"

cat >> "$tmp/owner.i" <<'EOF'
static inline void disable_inline(void) { pci_dev_disable_bus_master(dev); }
EOF
expect_failure inline_helper_body check_fixture "$tmp/manifest"
cp "$tmp/original-owner.i" "$tmp/owner.i"

cat >> "$tmp/owner.i" <<'EOF'
pci_dev_disable_bus_master(dev);
EOF
expect_failure macro_helper check_fixture "$tmp/manifest"
cp "$tmp/original-owner.i" "$tmp/owner.i"

for mutation in return comma ternary cast paren; do
	cp "$tmp/original-owner.i" "$tmp/owner.i"
	case "$mutation" in
	return) printf '%s\n' 'void f(void) { return pci_dev_disable_bus_master(dev); }' >> "$tmp/owner.i" ;;
	comma) printf '%s\n' 'void f(void) { (prepare(), pci_dev_disable_bus_master(dev)); }' >> "$tmp/owner.i" ;;
	ternary) printf '%s\n' 'void f(void) { ready ? pci_dev_disable_bus_master(dev) : (void)0; }' >> "$tmp/owner.i" ;;
	cast) printf '%s\n' 'void f(void) { (void)pci_dev_disable_bus_master(dev); }' >> "$tmp/owner.i" ;;
	paren) printf '%s\n' 'void f(void) { (pci_dev_disable_bus_master(dev)); }' >> "$tmp/owner.i" ;;
	esac
	expect_failure "${mutation}_helper" check_fixture "$tmp/manifest"
done
cp "$tmp/original-owner.i" "$tmp/owner.i"

cat >> "$tmp/tree/src/owner.c" <<'EOF'
void disable_owner(void) { pci_dev_disable_bus_master(dev); }
EOF
expect_failure raw_disable_helper check_fixture "$tmp/manifest"
sed -i '$d' "$tmp/tree/src/owner.c"

cat >> "$tmp/tree/src/owner.c" <<'EOF'
static inline void disable_inline(void) { pci_dev_disable_bus_master(dev); }
EOF
expect_failure raw_inline_helper_body check_fixture "$tmp/manifest"
sed -i '$d' "$tmp/tree/src/owner.c"

# A helper stored as a callback is an ownership site even though there is no
# call expression at the point of assignment.
cp "$tmp/tree/src/owner.c" "$tmp/callback-owner-base.c"
cat > "$tmp/tree/src/owner.c" <<'EOF'
static struct device_operations callback_ops = {
	.final =
		pci_dev_request_bus_master,
};
EOF
expect_failure callback_missing check_fixture "$tmp/manifest"
grep -q '^+enable.*src/owner.c' "$tmp/callback_missing.out" || {
	echo "callback site was not reported in the raw inventory" >&2; exit 1;
}
cp "$tmp/callback-owner-base.c" "$tmp/tree/src/owner.c"

# A line break before the call parenthesis does not turn an invocation into a
# declaration. It remains an executable ownership site.
cat >> "$tmp/tree/src/owner.c" <<'EOF'
void split_line_call(void)
{
	pci_dev_disable_bus_master
		(dev);
}
EOF
expect_failure raw_split_line_helper check_fixture "$tmp/manifest"
grep -q '^+disable.*src/owner.c' "$tmp/raw_split_line_helper.out" || {
	echo "split-line helper call was not reported in the raw inventory" >&2
	exit 1
}
cp "$tmp/callback-owner-base.c" "$tmp/tree/src/owner.c"

# Tokens in comments, strings, and character literals are not executable
# ownership sites and must not perturb the inventory.
cp "$tmp/tree/src/owner.c" "$tmp/comment-token-owner-base.c"
cat > "$tmp/tree/src/owner.c" <<'EOF'
/* pci_dev_request_bus_master and
 * pci_dev_disable_bus_master(dev)
 */
static const char *helper_name = "pci_dev_request_bus_master";
static const int helper_initial = 'p'; // pci_dev_disable_bus_master(dev)
EOF
awk '$1 != "site"' "$tmp/manifest" > "$tmp/comment-token-manifest"
check_fixture "$tmp/comment-token-manifest" > /dev/null
cp "$tmp/comment-token-owner-base.c" "$tmp/tree/src/owner.c"

cat >> "$tmp/tree/src/owner.c" <<'EOF'
/* adjacent pci_dev_request_bus_master prose */
static const char *adjacent_text = "first pci_dev_disable_bus_master text";
EOF
check_fixture "$tmp/manifest" > /dev/null
sed -i 's/adjacent pci_dev_request_bus_master prose/unrelated changed comment/' \
	"$tmp/tree/src/owner.c"
sed -i 's/first pci_dev_disable_bus_master text/completely changed string/' \
	"$tmp/tree/src/owner.c"
check_fixture "$tmp/manifest" > /dev/null
cp "$tmp/comment-token-owner-base.c" "$tmp/tree/src/owner.c"

cat >> "$tmp/tree/src/owner.c" <<'EOF'
#define DISABLE_OWNER(dev) pci_dev_disable_bus_master(dev)
EOF
expect_failure raw_macro_helper check_fixture "$tmp/manifest"
sed -i '$d' "$tmp/tree/src/owner.c"

for mutation in return comma ternary cast paren; do
	case "$mutation" in
	return) line='void f(void) { return pci_dev_disable_bus_master(dev); }' ;;
	comma) line='void f(void) { (prepare(), pci_dev_disable_bus_master(dev)); }' ;;
	ternary) line='void f(void) { ready ? pci_dev_disable_bus_master(dev) : (void)0; }' ;;
	cast) line='void f(void) { (void)pci_dev_disable_bus_master(dev); }' ;;
	paren) line='void f(void) { (pci_dev_disable_bus_master(dev)); }' ;;
	esac
	printf '%s\n' "$line" >> "$tmp/tree/src/owner.c"
	expect_failure "raw_${mutation}_helper" check_fixture "$tmp/manifest"
	sed -i '$d' "$tmp/tree/src/owner.c"
done

printf 'ramstage\t%s\nramstage\t%s/missing.i\n' "$tmp/owner.i" "$tmp" \
	> "$tmp/preprocessed-list"
expect_failure stale_preprocessed check_fixture "$tmp/manifest"
printf 'ramstage\t%s\n' "$tmp/owner.i" > "$tmp/preprocessed-list"

printf '%s\n' 'mutated generated static devicetree' > "$tmp/static.c"
expect_failure devicetree_drift check_fixture "$tmp/manifest"
printf '%s\n' 'generated static devicetree fixture' > "$tmp/static.c"

cat >> "$tmp/owner.i" <<'EOF'
pci_or_config8(dev, 4, (1U << 2));
EOF
expect_failure config8_command check_fixture "$tmp/manifest"
if grep -q command-overlap "$tmp/config8_command.out"; then
	echo "config8 at command start was misclassified" >&2; exit 1
fi
grep -q '^+enable.*command-register' "$tmp/config8_command.out" || {
	echo "config8 BME bit was not derived" >&2; exit 1;
}
cp "$tmp/original-owner.i" "$tmp/owner.i"

for mutation in 'overlap16_at3:16:3:10' 'overlap32_at1:32:1:26' \
	'overlap32_at2:32:2:18' 'overlap32_at3:32:3:10' \
	'overlap32_at4:32:4:2'; do
	name=${mutation%%:*}
	rest=${mutation#*:}; width=${rest%%:*}
	rest=${rest#*:}; offset=${rest%%:*}; bit=${rest##*:}
	printf 'pci_or_config%s(dev, %s, (1U << %s));\n' "$width" "$offset" "$bit" \
		>> "$tmp/owner.i"
	expect_failure "$name" check_fixture "$tmp/manifest"
	if [ "$offset" -ne 4 ]; then
		grep -q '^+enable.*command-overlap' "$tmp/$name.out" || {
			echo "unaligned access was not classified as overlap: $name" >&2; exit 1;
		}
	else
		grep -q '^+enable.*command-register' "$tmp/$name.out" || {
			echo "aligned dword BME bit was not derived" >&2; exit 1;
		}
	fi
	cp "$tmp/original-owner.i" "$tmp/owner.i"
done

cat >> "$tmp/owner.i" <<'EOF'
pci_write_config8(dev, 5, 0xff);
EOF
expect_failure overlap_byte5 check_fixture "$tmp/manifest"
grep -q '^+unknown.*command-overlap' "$tmp/overlap_byte5.out" || {
	echo "byte 5 access was not classified as command overlap" >&2; exit 1;
}
cp "$tmp/original-owner.i" "$tmp/owner.i"

# These byte ranges are statically disjoint from command bytes 4-5.
cat >> "$tmp/owner.i" <<'EOF'
pci_or_config8(dev, 3, 0xff);
pci_or_config8(dev, 6, 0xff);
pci_or_config16(dev, 2, 0xffff);
pci_or_config16(dev, 6, 0xffff);
pci_or_config32(dev, 0, 0xffffffff);
pci_or_config32(dev, 6, 0xffffffff);
EOF
check_fixture "$tmp/manifest" > /dev/null
cp "$tmp/original-owner.i" "$tmp/owner.i"

cat >> "$tmp/owner.i" <<'EOF'
pci_or_config16(dev, (2 + 2), (1U << 2));
EOF
expect_failure offset_sum check_fixture "$tmp/manifest"
if grep -q unknown-overlap "$tmp/offset_sum.out"; then
	echo "constant offset sum was not folded" >&2; exit 1
fi
cp "$tmp/original-owner.i" "$tmp/owner.i"

cat >> "$tmp/owner.i" <<'EOF'
pci_or_config16(dev, (4 + 0), 0x4);
EOF
expect_failure command_plus_zero check_fixture "$tmp/manifest"
if grep -q unknown-overlap "$tmp/command_plus_zero.out"; then
	echo "PCI_COMMAND plus zero was not folded" >&2; exit 1
fi
cp "$tmp/original-owner.i" "$tmp/owner.i"

cat >> "$tmp/owner.i" <<'EOF'
enum { enum_offset = 2 + 2 };
pci_or_config16(dev, enum_offset, 0x4);
EOF
expect_failure enum_offset check_fixture "$tmp/manifest"
if grep -q unknown-overlap "$tmp/enum_offset.out"; then
	echo "enum offset was not folded" >&2; exit 1
fi
cp "$tmp/original-owner.i" "$tmp/owner.i"

cat >> "$tmp/owner.i" <<'EOF'
pci_write_config16(dev, offset, 0x4);
EOF
expect_failure variable_offset check_fixture "$tmp/manifest"
grep -q unknown-overlap "$tmp/variable_offset.out" || {
	echo "variable offset was not classified conservatively" >&2; exit 1;
}
cp "$tmp/original-owner.i" "$tmp/owner.i"

cat >> "$tmp/owner.i" <<'EOF'
pci_or_config16(dev, 6, 0x4); pci_write_config16(dev, offset, 0x4);
EOF
expect_failure same_line_offset check_fixture "$tmp/manifest"
grep -q unknown-overlap "$tmp/same_line_offset.out" || {
	echo "second same-line offset escaped classification" >&2; exit 1;
}
cp "$tmp/original-owner.i" "$tmp/owner.i"

# A statically non-command register does not enter the BME inventory.
cat >> "$tmp/owner.i" <<'EOF'
pci_or_config16(dev, 6, 0x4);
EOF
check_fixture "$tmp/manifest" > /dev/null
cp "$tmp/original-owner.i" "$tmp/owner.i"

cat >> "$tmp/owner.i" <<EOF
# 1 "$tmp/tree/src/helper.h"
static inline void helper(void) { pci_or_config16(dev, 4, (1U << 2)); }
# 9 "$tmp/tree/src/owner.c"
helper();
EOF
expect_failure header_helper check_fixture "$tmp/manifest"
cp "$tmp/original-owner.i" "$tmp/owner.i"

cat >> "$tmp/owner.i" <<EOF
# 1 "$tmp/build/generated-bme.h"
pci_update_config16(dev, 4, ~0, (1 << 2));
EOF
expect_failure generated_include check_fixture "$tmp/manifest"
cp "$tmp/original-owner.i" "$tmp/owner.i"

awk '$1 != "site"' "$tmp/manifest" > "$tmp/missing"
expect_failure missing check_fixture "$tmp/missing"

cp "$tmp/manifest" "$tmp/duplicate"
grep '^site' "$tmp/manifest" >> "$tmp/duplicate"
expect_failure duplicate check_fixture "$tmp/duplicate"

sed 's#src/owner.c#src/missing.c#' "$tmp/manifest" > "$tmp/stale"
expect_failure stale check_fixture "$tmp/stale"

awk '$1 != "site"' "$tmp/manifest" > "$tmp/comment"
printf '# site%s' "$tab" >> "$tmp/comment"
grep '^site' "$tmp/manifest" | cut -f2- >> "$tmp/comment"
expect_failure comment check_fixture "$tmp/comment"

sed 's#src/owner.c#../owner.c#' "$tmp/manifest" > "$tmp/escape"
expect_failure path_escape check_fixture "$tmp/escape"

sed 's/CONFIG_PAYLOAD_OWNS_PCI_DEVICES=y/# CONFIG_PAYLOAD_OWNS_PCI_DEVICES is not set/' \
	"$tmp/config" > "$tmp/config-drift"
expect_failure config_branch "$checker" "$tmp/manifest" "$tmp/tree" \
	"$tmp/config-drift" "$tmp/sources" "$tmp/preprocessed-list" "$tmp/static.c"

cp "$tmp/tree/src/owner.c" "$tmp/original-owner.c"
cat >> "$tmp/tree/src/owner.c" <<'EOF'
void new_owner(void)
{
	pci_or_config16(other, PCI_COMMAND, PCI_COMMAND_MASTER);
}
EOF
expect_failure new_site check_fixture "$tmp/manifest"
cp "$tmp/original-owner.c" "$tmp/tree/src/owner.c"

sed 's/pci_or_config16/\/\/ pci_or_config16/' "$tmp/tree/src/owner.c" \
	> "$tmp/comment-owner.c"
cp "$tmp/comment-owner.c" "$tmp/tree/src/owner.c"
expect_failure prose_evidence check_fixture "$tmp/manifest"

grep -q 'b4492d5cb26847b628cdf8c4252a4331359ea385' \
	"$root/payload/cdk2-nvme-ownership.md"
grep -q 'external evidence, not part of the coreboot ownership gate' \
	"$root/payload/cdk2-nvme-ownership.md"

echo "PCI bus-master ownership mutation tests passed"
