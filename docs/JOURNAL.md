# Journal

Chronological high-level action log for this tree. Detailed per-action records
live under `.logs/`; the durable reference for the patch stack is
[`docs/cachyos-stack.md`](cachyos-stack.md).

- 2026-11-05 — repo created: upstream v7.4-rc1 (05441d697f7f) + 58 CachyOS
  commits + BORE; tag `catchya/base-2026-11-05`; pushed to origin.
- 2026-11-05 — `media: i2c: max96717: fix bitfield overflow` (299f0b100cd8):
  AutoFDO + ThinLTO turned a `field_prep()` overflow into a link error;
  reproduced with the recipe's `afdo.prof`, submitted upstream.
- 2026-11-06 — measurement harness committed (`tools/exp/`), noise floor
  measured and recorded; sterile A/B judged impossible on a daily-use laptop.
- 2026-11-06 — reference book for the stack written (`docs/cachyos-stack.md`)
  and gap-analysed against `cachyos-7.4-rc1-1`: CachyOS dropped BORE and POC for
  7.4 and moved to tuned EEVDF; eight commits identified as worth picking.
- 2026-11-06 — fork master synced to torvalds master (`c463b80881fb`); the eight
  commits identified in the gap analysis were cherry-picked and rebased on top
  (xHCI ownership, four mm defaults, 256 KiB readahead, Rust+BTF+LTO, EEVDF
  interactivity tuning).
- 2026-11-06 — scheduler decision: BORE is the tree's scheduler (`CONFIG_SCHED_BORE`
  in-tree, POC on). The EEVDF tuning pick was dropped by rebase and force-pushed;
  under BORE its block does not compile, so keeping it was dead code.
- 2026-11-06 — AutoFDO + Rust build fix (`55ee2831893d`): Rust 1.100 stabilized
  `-Zprofile-sample-use` as `-Cprofile-sample-use`, and LLVM 24 renamed
  `-split-machine-functions` to `-function-splitting=all`; both spellings in
  `scripts/Makefile.autofdo` were rejected, breaking `make prepare` at
  `rust/core.o`. Version-gated the profile flag, probed the LLVM arg with
  `rustc-option`; red/green verified against `make prepare` — see
  `.logs/action-2026-11-06-autofdo-rust-flags.md`.
- 2026-11-07 — out-of-tree patch book (`86f41fcaf565`, `docs/out-of-tree-patches.md`):
  every patch the tree carries that torvalds/linux does not have, with measured
  provenance and rebase facts. BORE = published 7.4 patch + one conflict
  resolution + the `!DEQUEUE_DELAYED` guard; POC = abandoned 7.3 commit with a
  byte-identical 2038-line file and no patch source; NVIDIA patch list drifted
  (7.4 ships 0001/0002/0004/0005, recipe pins 0001-0003); intake protocol and
  retired-patch ledger added.
- 2026-11-07 — mm out-of-tree import (`61e1870a62df`, `90c8b34dd9df`,
  `8b143fc6fce5`; branches `mm/harness`, `mm/thp-async-defrag`,
  `mm/prezero-pages`, integrated on `mm/outsource-imports`, nothing pushed).
  Audit first: MGLRU is already in-tree and tuned (no backport exists), THP
  async defrag is one policy default plus a retry suppression, idle
  pre-zeroing is a new subsystem. Built with the recipe config and booted under
  QEMU: `defrag` reads `[defer]`, the zero invariant holds over 163840 pages,
  the debugfs counters show pages filled and consumed, and `init_on_free=1` /
  `page_poison=1` both disable the feature — see
  `.logs/action-2026-11-07-mm-imports.md` and `docs/out-of-tree-patches.md`
  §3.5/§3.6.
- 2026-11-07 — AMD Strix Point intake (`1181bc1cefd2`, `91e3309dcd78`,
  `50553fea7216`, `d3174f9cb969`; branches `amd/harness`, `amd/gfxoff-knob`,
  `sched/futex-affine`, integrated on `amd/strix-point` over
  `mm/outsource-imports`, nothing pushed).
  Five requested items became one knob, one parked prototype, two "already
  present" and one refusal: `ntsync` and the TTM UMA behaviour are in-tree
  (config assertion and a QEMU ioctl probe added), GFXOFF entry delay is now
  `amdgpu_gfx_off_delay_ms` (0-1000, default unchanged; built as =m and =y,
  module loaded in QEMU, range enforced), the futex LLC hint moved 999/1000
  wakeups into the waker's LLC in a 2-LLC QEMU boot (0/1000 without it) but
  has no latency verdict without hardware, and DCN commit pruning is refused
  with its reopen condition. See `.logs/action-2026-11-07-strix-point.md` and
  `docs/out-of-tree-patches.md` §10.
- 2026-11-07 — README prepended with the project section (`89aa2a550f47`):
  goal, the optimisation list with what each item buys, the two refusals, the
  build recipe, and pointers to the ledger/stack book/journal/tools. Linus'
  upstream text is preserved byte-for-byte below a separator.
- 2026-11-07 — sync with upstream `d48723711bae` (`da3ff084cc1f`, 327 commits
  since `eedaa7d61f1e`). One conflict, `drivers/iommu/amd/iommu.c`: the 7.3
  CachyOS revert of the GPU identity-mode quirk is retired — CachyOS never
  carried it past 7.3 and upstream reworked the same code for PerfOpt
  (`b32ecac779bb`, `2cf4be57b301`). Full recipe-config build clean (0 errors,
  0 warnings); QEMU boot passes the zero invariant and the 2-LLC futex
  placement check. Recorded in `docs/out-of-tree-patches.md` §8 and
  `docs/cachyos-stack.md` §3.6. `dkms-clang.patch` regenerated in the recipe
  (`21e93a8`) because upstream moved its context line; prepare re-run green
  with 113/113 expectations. Evidence:
  `.logs/action-2026-11-07-upstream-sync.md`.
- 2026-11-07 — package rebuilt from the synced tree (`da3ff084cc1f` base,
  recipe `6ec10d2`), and verified: full 1–5 run green with both profile stages
  on the designed `~/.reserve/Kernel_PGO/` fallback (microVMs cannot capture
  LBR), BBR3 found unbuilt and then found unbuildable as `=y` — its copied
  `BTF_ID_FLAGS(func, bbr_*)` collide with BBR's and break `resolve_btfids` —
  so it ships as `tcp_bbr3.ko`. Packaged vmlinuz boot-tested: zero invariant
  PASS, futex placement 0/1000 → 999/1000. See
  `.logs/action-2026-11-07-resync-package.md`.
- 2026-11-10 — six compiler/kernel-shape tracks closed: ICP moves 0 bytes on
  the shipped profile and is blocked on profile fidelity (`.text` byte-identical
  with and without; the profile carries no indirect-target records; the brief's
  flag names do not exist in LLVM 24), `static_call`/`INDIRECT_CALL` already at
  its limit (68 sites; `f_op`/`sched_class` are per-instance),
  `TRIM_UNUSED_KSYMS` refused because the package serves DKMS, MFS shown
  splitting (1,676 `.cold` halves = 524,351 B into `.text.split`, image
  +0.054 %, no counters), SIMD verified at runtime
  (`gcm(aes)` → `generic-gcm-vaes-avx512`), mitigation pruning already at the
  endpoint. See `docs/out-of-tree-patches.md` §11 and
  `.logs/action-2026-11-10-optimisation-tracks.md`.
- 2026-11-10 — CachyOS sync check and a kbuild correctness fix: latest release
  is `cachyos-7.4-rc2-2` (`93b4b6f912da`, 7.4-rc2 + 135 commits); 45 of them
  are patch-identical to what we carry, 4 differ only by base adaptation, and
  the 86 new ones are 48 Apple T2, 8 Samsung, 11 wrong-vendor, 8 repo tooling,
  2 taken and 9 single-commit refusals. The two sched-defaults picks
  (cgroup_mode=max, DELAY_DEQUEUE off) were built together with the linker fix
  on `sync/cachyos-7.4-rc2-2` — 0 warnings, and the QEMU probe read back
  `(max)`, `NO_DELAY_DEQUEUE`, `NO_DELAY_ZERO` and BORE's node — then merged.
  Separately, the AutoFDO/Propeller fragments dropped every linker profile
  flag under Full LTO; the guard was verified with a makefile harness and lld,
  and fixed (ThinLTO output unchanged). See `docs/cachyos-stack.md` §3.10,
  `docs/out-of-tree-patches.md` §3.7 and
  `.logs/action-2026-11-10-cachyos-rc2-2-sync.md`.

- 2026-11-12 — diagnosed the AutoFDO + Propeller `vmlinux.o` failure
  (`objtool: __io_timeout_prep+0x1d2: can't find switch jump table`): Clang's
  dangling jump-table entries point one past the end of the *basic-block-split
  cold* section, which the upstream "Ignore dangling jump table entries"
  tolerance (`pfunc->offset + pfunc->len`) does not cover. `dad82eb1805d` is
  in rc1 too, so this is not an rc2 regression. Fix extends the check to
  `cfunc`; verified green on the failing link, no regression in the objtool
  suite (50/50) — see `.logs/action-2026-11-12-objtool-cold-jump-table.md`.
- 2026-11-12 — `tools/objtool-cold-jump-table` (`365d3e25312b`): the `cfunc`
  dangling-entry tolerance above, guarded so it only fires when the cold
  symbol reaches its section end (a split section can hold more than one cold
  fragment, and an interior fragment's end is live code). Ledger row 11 and
  section 3.8 in `docs/out-of-tree-patches.md`. Upstream submission held until
  the owner runs the real `makepkg`.
