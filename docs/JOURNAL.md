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
  interactivity tuning). Stack is now 66 CachyOS commits + 2 local.
