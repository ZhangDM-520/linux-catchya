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
# Environment:
#   SRC, BUILD, OUT   source tree, kernel build tree, output directory
#   KERNEL            bzImage (default $BUILD/arch/x86/boot/bzImage)
#   MEM, SMP, TMO     QEMU memory, vCPUs, wall-clock timeout
#   EXTRA_CMDLINE     appended to the kernel command line
set -uo pipefail

SRC="${SRC:-$HOME/Projects/linux-catchya}"
BUILD="${BUILD:-$HOME/Workspace/mm-build}"
OUT="${OUT:-$SRC/.logs/exp/vm}"
KERNEL="${KERNEL:-$BUILD/arch/x86/boot/bzImage}"
MEM="${MEM:-2048}"
SMP="${SMP:-2}"
TMO="${TMO:-300}"
EXTRA_CMDLINE="${EXTRA_CMDLINE:-}"
CMDLINE="console=ttyS0,115200 rdinit=/init panic=1 oops=panic \
loglevel=7 init_on_alloc=1 $EXTRA_CMDLINE"

fail() { echo "FAIL: $*" >&2; exit 1; }

[ -r "$KERNEL" ] || fail "no kernel image at $KERNEL"
command -v qemu-system-x86_64 >/dev/null || fail "qemu-system-x86_64 not found"

mkdir -p "$OUT/rootfs"
rm -f "$OUT/rootfs/init" "$OUT/initramfs.cpio.gz" "$OUT/serial.log"
cc -O2 -static -o "$OUT/rootfs/init" "$SRC/tools/exp/vmprobe.c" \
	|| fail "cannot build the static probe"
install -m 0755 "$OUT/rootfs/init" "$OUT/init"

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
grep -q "PASS zero invariant" "$OUT/serial.log" \
	|| { echo "FAIL: zero invariant did not report PASS"; bad=1; }
grep -q "vmprobe: done" "$OUT/serial.log" \
	|| { echo "FAIL: probe did not finish"; bad=1; }
if grep -nE "cut here|Kernel panic|BUG: |Oops: |Call Trace:" \
	"$OUT/serial.log"; then
	echo "FAIL: kernel complained (see lines above)"
	bad=1
fi
grep -E "^(PASS|FAIL) |^fault " "$OUT/serial.log"
[ "$bad" = 0 ] && echo "== PASS" || echo "== FAIL"
exit "$bad"
