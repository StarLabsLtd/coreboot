#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT HUP INT TERM

for optimization in 0 2; do
	cc -std=gnu11 -Wall -Wextra -Werror -O"$optimization" \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-I"$root" "$root/tests/policy_test.c" "$root/policy.c" \
		-o "$tmp/policy-test-$optimization" \
		$(pkg-config --cflags --libs libcrypto)
	ASAN_OPTIONS=detect_leaks=1 "$tmp/policy-test-$optimization"
done

cc -std=gnu11 -Wall -Wextra -Werror -O2 -I"$root" \
	-fsanitize=address,undefined -fno-omit-frame-pointer \
	"$root/main.c" "$root/policy.c" -o "$tmp/capsule-tpm-provision" \
	$(pkg-config --cflags --libs libcrypto)
cc -std=gnu11 -Wall -Wextra -Werror -O2 \
	"$root/tests/make_socket.c" -o "$tmp/make-socket"

manifest=$(printf '11%.0s' $(seq 1 32))
modulus=80$(printf '01%.0s' $(seq 1 254))03
args="--index 0x01001234 --epoch 1 --manifest-sha256 $manifest"
args="$args --rsa-modulus $modulus --policy-ref 63617073756c65"

# The argument list is fixed test data without shell metacharacters.
# shellcheck disable=SC2086
"$tmp/capsule-tpm-provision" generate $args --output "$tmp/artifacts"
# shellcheck disable=SC2086
"$tmp/capsule-tpm-provision" validate $args --output "$tmp/artifacts"
# shellcheck disable=SC2086
"$tmp/capsule-tpm-provision" generate $args --output "$tmp/artifacts-2"
for source in "$tmp/artifacts"/*; do
	cmp "$source" "$tmp/artifacts-2/$(basename "$source")"
done

if "$tmp/capsule-tpm-provision" generate $args --output "$tmp/artifacts" \
	2>/dev/null; then
	echo "generator overwrote an existing directory" >&2
	exit 1
fi

for source in "$tmp/artifacts"/*; do
	artifact=$(basename "$source")
	rm -rf "$tmp/mutated"
	cp -a "$tmp/artifacts" "$tmp/mutated"
	old=$(od -An -tu1 -N1 "$tmp/mutated/$artifact")
	new=$((($old + 1) % 256))
	printf "\\$(printf '%03o' "$new")" | dd \
		of="$tmp/mutated/$artifact" bs=1 seek=0 conv=notrunc status=none
	if "$tmp/capsule-tpm-provision" validate $args \
		--output "$tmp/mutated" 2>/dev/null; then
		echo "validator accepted mutated $artifact" >&2
		exit 1
	fi
done

cp -a "$tmp/artifacts" "$tmp/truncated"
truncate -s 31 "$tmp/truncated/auth-policy.bin"
if "$tmp/capsule-tpm-provision" validate $args --output "$tmp/truncated" \
	2>/dev/null; then
	echo "validator accepted a truncated artifact" >&2
	exit 1
fi

cp -a "$tmp/artifacts" "$tmp/extended"
printf '\000' >> "$tmp/extended/auth-policy.bin"
if "$tmp/capsule-tpm-provision" validate $args --output "$tmp/extended" \
	2>/dev/null; then
	echo "validator accepted an extended artifact" >&2
	exit 1
fi

cp -a "$tmp/artifacts" "$tmp/symlink"
rm "$tmp/symlink/auth-policy.bin"
ln -s ../artifacts/auth-policy.bin "$tmp/symlink/auth-policy.bin"
if "$tmp/capsule-tpm-provision" validate $args --output "$tmp/symlink" \
	2>/dev/null; then
	echo "validator followed an artifact symlink" >&2
	exit 1
fi

cp -a "$tmp/artifacts" "$tmp/missing"
rm "$tmp/missing/auth-policy.bin"
if "$tmp/capsule-tpm-provision" validate $args --output "$tmp/missing" \
	2>/dev/null; then
	echo "validator accepted a missing artifact" >&2
	exit 1
fi

cp -a "$tmp/artifacts" "$tmp/extra"
touch "$tmp/extra/policy.o"
if "$tmp/capsule-tpm-provision" validate $args --output "$tmp/extra" \
	2>/dev/null; then
	echo "validator accepted an unknown build artifact" >&2
	exit 1
fi

cp -a "$tmp/artifacts" "$tmp/fifo"
rm "$tmp/fifo/auth-policy.bin"
mkfifo "$tmp/fifo/auth-policy.bin"
if "$tmp/capsule-tpm-provision" validate $args --output "$tmp/fifo" \
	2>/dev/null; then
	echo "validator accepted a FIFO artifact" >&2
	exit 1
fi

cp -a "$tmp/artifacts" "$tmp/socket"
rm "$tmp/socket/auth-policy.bin"
"$tmp/make-socket" "$tmp/socket/auth-policy.bin"
if "$tmp/capsule-tpm-provision" validate $args --output "$tmp/socket" \
	2>/dev/null; then
	echo "validator accepted a socket artifact" >&2
	exit 1
fi

cp -a "$tmp/artifacts" "$tmp/directory"
rm "$tmp/directory/auth-policy.bin"
mkdir "$tmp/directory/auth-policy.bin"
if "$tmp/capsule-tpm-provision" validate $args --output "$tmp/directory" \
	2>/dev/null; then
	echo "validator accepted a directory artifact" >&2
	exit 1
fi

cp -a "$tmp/artifacts" "$tmp/device"
rm "$tmp/device/auth-policy.bin"
if mknod "$tmp/device/auth-policy.bin" c 1 3 2>/dev/null; then
	if "$tmp/capsule-tpm-provision" validate $args \
		--output "$tmp/device" 2>/dev/null; then
		echo "validator accepted a device artifact" >&2
		exit 1
	fi
fi
