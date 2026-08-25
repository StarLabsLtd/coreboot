#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT

cd "$temporary"
"$root/util/validate-starbook-mtl-spi-console.sh"

printf '%s\n' 'StarBook MTL out-of-tree SPI console validation passed'
