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
