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
