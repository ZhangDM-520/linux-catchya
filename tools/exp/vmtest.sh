#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# vmtest.sh - boot the built kernel under QEMU with tools/exp/vmprobe.c as
# init and check the mm invariants the pre-zeroing and THP patches touch.
#
# The kernel is booted with an initramfs whose /init is the probe, so the
# result does not depend on any user space of the host. The probe prints a
# fixed set of lines; this script fails on a missing PASS, on a kernel BUG or
# WARNING, and on a boot that does not finish.
#
# Usage:
#   tools/exp/vmtest.sh
#   EXTRA_CMDLINE="init_on_free=1" tools/exp/vmtest.sh
#   BUILD=/path/to/build OUT=/tmp/vm tools/exp/vmtest.sh
#
#   # a different probe, with its module packed into the initramfs:
#   INIT_SRC=tools/exp/ntsync_probe.c EXPECT="PASS ntsync" DONE="ntsync_probe: done" \
#       WITH_MODULES=ntsync tools/exp/vmtest.sh
#
# Environment:
#   SRC, BUILD, OUT   source tree, kernel build tree, output directory
#   KERNEL            bzImage (default $BUILD/arch/x86/boot/bzImage)
#   MEM, SMP, TMO     QEMU memory, vCPUs, wall-clock timeout
#   EXTRA_CMDLINE     appended to the kernel command line
#   INIT_SRC          probe source, compiled static and used as /init
#   EXPECT, DONE      serial-log patterns that must appear
#   WITH_MODULES      comma list of module names to pack and load
#   STRIP_MODULES     strip debug info from the packed copies; the build tree
#                     has CONFIG_DEBUG_INFO=y, so amdgpu.ko alone is 713 MiB
set -uo pipefail

SRC="${SRC:-$HOME/Projects/linux-catchya}"
BUILD="${BUILD:-$HOME/Workspace/mm-build}"
OUT="${OUT:-$SRC/.logs/exp/vm}"
KERNEL="${KERNEL:-$BUILD/arch/x86/boot/bzImage}"
MEM="${MEM:-2048}"
SMP="${SMP:-2}"
TMO="${TMO:-300}"
EXTRA_CMDLINE="${EXTRA_CMDLINE:-}"
INIT_SRC="${INIT_SRC:-$SRC/tools/exp/vmprobe.c}"
EXPECT="${EXPECT:-PASS zero invariant}"
DONE="${DONE:-vmprobe: done}"
WITH_MODULES="${WITH_MODULES:-}"
STRIP_MODULES="${STRIP_MODULES:-0}"
CMDLINE="console=ttyS0,115200 rdinit=/init panic=1 oops=panic \
loglevel=7 init_on_alloc=1 $EXTRA_CMDLINE"

fail() { echo "FAIL: $*" >&2; exit 1; }

[ -r "$KERNEL" ] || fail "no kernel image at $KERNEL"
command -v qemu-system-x86_64 >/dev/null || fail "qemu-system-x86_64 not found"

mkdir -p "$OUT/rootfs"
rm -rf "$OUT/rootfs/modules" "$OUT/rootfs/init" \
	"$OUT/initramfs.cpio.gz" "$OUT/serial.log"
cc -O2 -static -o "$OUT/rootfs/init" "$INIT_SRC" \
	|| fail "cannot build the static probe from $INIT_SRC"
install -m 0755 "$OUT/rootfs/init" "$OUT/init"

if [ -n "$WITH_MODULES" ]; then
	mkdir -p "$OUT/rootfs/modules"
	i=0
	for m in ${WITH_MODULES//,/ }; do
		f=$(find "$BUILD" -name "$m.ko" -print -quit)
		[ -n "$f" ] || fail "no $m.ko under $BUILD"
		i=$((i + 1))
		dst="$OUT/rootfs/modules/$(printf '%02d' "$i")-$m.ko"
		cp "$f" "$dst"
		if [ "$STRIP_MODULES" = 1 ]; then
			objcopy --strip-debug "$dst" \
				|| fail "cannot strip $(basename "$f")"
		fi
		echo "== packing $(basename "$f")"
	done
fi

(
	cd "$OUT/rootfs" || exit 1
	find . -print0 | cpio --null -o --format=newc 2>/dev/null | gzip -9
) >"$OUT/initramfs.cpio.gz" || fail "cannot build the initramfs"

echo "== booting $KERNEL"
echo "== cmdline: $CMDLINE"
timeout "$TMO" qemu-system-x86_64 \
	-machine q35,accel=kvm:tcg -cpu max \
	-m "$MEM" -smp "$SMP" \
	-kernel "$KERNEL" -initrd "$OUT/initramfs.cpio.gz" \
	-append "$CMDLINE" \
	-no-reboot -display none -monitor none -serial stdio \
	2>&1 | tee "$OUT/serial.log"
rc=${PIPESTATUS[0]}
[ "$rc" = 124 ] && echo "== qemu timed out after ${TMO}s"

echo "== checks"
bad=0
grep -q "$EXPECT" "$OUT/serial.log" \
	|| { echo "FAIL: expected '$EXPECT' not in the log"; bad=1; }
grep -q "$DONE" "$OUT/serial.log" \
	|| { echo "FAIL: expected '$DONE' not in the log"; bad=1; }
grep -qE "^FAIL " "$OUT/serial.log" \
	&& { echo "FAIL: the probe reported a failure"; bad=1; }
if grep -nE "cut here|Kernel panic|BUG: |Oops: |Call Trace:" \
	"$OUT/serial.log"; then
	echo "FAIL: kernel complained (see lines above)"
	bad=1
fi
grep -E "^(PASS|FAIL|SKIP) |^fault |^module " "$OUT/serial.log"
[ "$bad" = 0 ] && echo "== PASS" || echo "== FAIL"
exit "$bad"
