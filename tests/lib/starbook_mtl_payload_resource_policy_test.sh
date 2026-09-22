#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

cc -std=gnu11 -Wall -Wextra -Werror \
	-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
	"$root/tests/lib/starbook_mtl_payload_resource_policy_test.c" \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/payload_resource_policy.c" \
	-o "$temporary/test"
"$temporary/test"
printf '%s\n' 'StarBook MTL payload-resource policy tests: PASS'
