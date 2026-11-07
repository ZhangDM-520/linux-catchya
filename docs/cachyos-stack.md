# The catchya CachyOS stack — reference book

Living reference for the CachyOS commits carried on `master` of this tree, plus
the local deltas on top. It exists so that each commit can be explained,
re-verified, and re-evaluated against a newer CachyOS release without repeating
the original archaeology.

Companion book: `docs/out-of-tree-patches.md` covers everything this tree
carries that torvalds/linux does not have and this book does not own — BORE,
the POC selector, our own fixes, and the patches the recipe applies at build
time.

Last reviewed: 2026-11-06, against `cachyos-7.4-rc1-1`. The 58 original commits
were replayed onto the fork's synced master (torvalds master merged at
`c463b80881fb`), and the eight commits recommended in section 6 were then
cherry-picked on top — and the EEVDF tuning pick was then dropped, so the current stack is **65 CachyOS commits + 2 local**.

---

## 1. What this tree is

```
master = torvalds master (merged into the fork at d48723711bae, v7.4-rc2 era)
       + 58 CachyOS commits (cherry-pick -x, adapted where noted)
       + 2 local commits (max96717 fix, measurement harness)
       + 7 CachyOS commits picked 2026-11-06 (section 3.9)
```

| Item | Value |
|---|---|
| Base tag | `catchya/base-2026-11-05` (05441d697f7f); fork master merged upstream at c463b80881fb, then again at d48723711bae (2026-11-07) |
| Remotes | `origin` = ZhangDM-520/linux-catchya, `upstream` = torvalds/linux, `cachyos` = CachyOS/linux |
| Source CachyOS release | `cachyos-7.4-rc1-1` (9b0672698044), itself 7.4-rc1 + 130 commits |
| Merge base with upstream | `6628e7d4c6b2` (the v7.4-rc1 tag) |
| Rebase tooling | `tools/catchya-sync fetch` / `rebase` (rerere on, never pushes) |

**Invariants**

1. Every CachyOS commit keeps its "(cherry picked from commit ...)" trailer, so
   `git log --grep "cherry picked from"` is the provenance index.
2. Manual adaptations are squashed into the commit they belong to, never added
   as a separate fixup commit on master. The adaptation is described in the
   commit body and in section 5 of this book.
3. No `Signed-off-by` from automation. Human DCO only.
4. `master` must build and boot before any experiment branch is created.

---

## 2. Coverage at a glance

| CachyOS branch | Commits in 7.4-rc1-1 | Carried | Comment |
|---|---:|---:|---|
| `7.4/cachy` | 50 | 29 | 23 patch-identical + 6 present in adapted form (POC, ACS + fixup, ADIOS + fixup, pipeliner). Not carried: 8 tag-release, 9 hardware-n/a (intel-pstate, VHBA, Intel NVMe, …), 2 compiler-n/a (Zen4 ISA, GCC SMS), 1 POC revert, 1 Rust BTF |
| `7.4/fixes` | 13 | 5 | 8 not applicable or not needed on this machine |
| `7.4/vesa-dsc-bpp` | 8 | 8 | complete |
| `7.4/bbr3` | 2 | 2 | complete |
| `7.4/snd-codecs` | 8 | 0 | Samsung Galaxy Book codecs — not this laptop |
| `7.4/t2` | 49 | 0 | Apple T2 — not this laptop |
| zram (no 7.4 branch; content present) | — | 11 | carried from `7.3/zram` |
| hdmi / passive VRR (no 7.4 branch) | — | 3 | carried from `7.3/hdmi`; **not** in the 7.4 release |
| sched / BORE (no 7.4 branch) | — | 1 | patch-file based; **not** in the 7.4 release |

58 original: 5 `fixes` + 8 `vesa-dsc-bpp` + 2 `bbr3` + 23 `cachy` + 4
`cachy`-adapted (POC, ADIOS, ACS, pipeliner) + 11 zram + 3 hdmi + 1 bore +
1 iommu revert = 58. Plus 8 picked on 2026-11-06 (section 3.9) minus the EEVDF
tuning commit dropped the same day = 65. The iommu revert was retired at the
2026-11-07 upstream merge, so the carried count is 64.

---

## 3. The 58 commits, grouped by subsystem

"Files" is the number of files touched, as a blast-radius indicator.
"Source" is the CachyOS topic branch, or **adapted** where the tree does not
apply cleanly or the branch does not exist for 7.4. For strict history order:
`git log --reverse --oneline catchya/base-2026-11-05..HEAD`.

### 3.1 Configuration surface

| SHA | Subject | Files | Source |
|---|---|---:|---|
| `a57da071b46d` | Kconfig.hz: Add additional timer frequency options | 1 | `cachy` |
| `4ce65652e37f` | Kconfig: Restore CONFIG_OPTIMIZE_FOR_PERFORMANCE_O3 | 2 | `cachy` |
| `6d9f3b595134` | Kconfig.preempt: Remove CONFIG_EXPERT dependency from PREEMPT_RT | 1 | `cachy` |
| `3975e0b13ef9` | CACHY: v4l2-core: add v4l2loopback | 5 | `cachy` |
| `5279103a2e6c` | Kconfig: Add CONFIG_CACHY | 1 | `cachy` |
| `9523c1364ef5` | CACHY: Disable split lock mitigation by default | 1 | `cachy` |
| `0d427cecf390` | add sysctl to allow disabling unprivileged CLONE_NEWUSER | 1 | `cachy` |

`CONFIG_CACHY` is the gate every other CACHY patch keys off. The tree actually
sets `CONFIG_CACHY=y`, `CONFIG_HZ=600` and
`CONFIG_CC_OPTIMIZE_FOR_PERFORMANCE_O3=y`.

### 3.2 Scheduler, TSC and branch hints

| SHA | Subject | Files | Source |
|---|---|---:|---|
| `c0e7962295db` | bore-cachy | 14 | **adapted** (patch file) |
| `1a3b10033f63` | 7.2-rc1-poc-selector-v2.6.3 | 8 | **adapted** (`cachy`) |
| `df82711c6e90` | sched/core: add some branch hints based on gcov analysis | 1 | `cachy` |
| `bd0b27f3ab09` | list: Mark some functions as __always_inline | 1 | `cachy` |
| `721fa88c6f30` | readdir: add unlikely hint on len check | 1 | `cachy` |
| `879f2f527bcb` | sched/core: Make finish_task_switch() and its subfunctions always inline | 11 | `fixes` |
| `007d15826b27` | sched/fair: do not scan twice in detach_tasks() | 1 | `fixes` |
| `2cf81918b331` | x86: implement tsc=directsync for systems without IA32_TSC_ADJUST | 4 | `cachy` |
| `18316e1d0fcb` | x86: touch clocksource watchdog after syncing TSCs | 1 | `cachy` |
| `4a699b1c8f3d` | x86: save/restore TSC counter value during sleep/wake | 3 | `cachy` |
| `05d45678cee5` | x86: only restore TSC if we have IA32_TSC_ADJUST or directsync enabled | 1 | `cachy` |
| `805551ef792b` | x86: don't check for random warps if using direct sync | 1 | `cachy` |
| `1561d532602c` | x86: revert extra time added to check for tsc wraps | 1 | `cachy` |

- **BORE** is runtime-toggleable and **off** in the shipped config
  (`CONFIG_SCHED_BORE` not set). It exists so the scheduler can be A/B tested
  without a rebuild.
- **POC selector** is **on** (`CONFIG_SCHED_POC_SELECTOR=y`). CachyOS reverted
  it for 7.4 — see section 5.1.
- The TSC series only matters with `tsc=directsync` on the command line; it is
  dormant otherwise.

### 3.3 Memory management and block

| SHA | Subject | Files | Source |
|---|---|---:|---|
| `9d886921c7b1` | CACHY: mm: lru-gen: Protect the working set of the last 100 jiffies | 1 | `cachy` |
| `f1191391a0a7` | mm/swap_state: Disable swap-in readahead | 1 | `cachy` |
| `392d7626abd5` | CACHY: Use BFQ for SQ devices and mq-deadline for MQ devices | 1 | `cachy` |
| `2cc16148cc2c` | iosched-7.3: update ADIOS to 3.3.0 | 1 | **adapted** (`cachy`) |
| `09cc44c90059` | linux7.1-rc1-ADIOS-3.2.0 | 4 | `cachy` |
| `99daf3fbd22a` | Input: evdev - use call_rcu when detaching client | 1 | `cachy` |

ADIOS is a BFQ-derived I/O scheduler. Our `block/adios.c` is byte-identical to
the 7.4 release tree (`git diff` empty), so the two ADIOS commits are one
logical update split for history.

### 3.4 zram

`c9821ee4f204` rw_semaphore for decompression streams ·
`5c16d407d0af` singleton compression context for recompression ·
`61134ad3c6ff` split zcomp into R/W streams ·
`b14aeaf2dc00` reset writeback state in reset_device ·
`d42f0db4d59b` zstd: no kvzalloc ·
`9e5079d7ee9f` lz4hc: LZ4_decompress_safe_usingDict ·
`d1a8dc17143c` lz4: same ·
`a1ed3f3830f1` lz4hc: pre-initialise dictionary stream ·
`870b183aeef4` zstd: no empty C/D dictionaries ·
`20e36b54d9ad` remove debugfs entry on init failure ·
`eac5bf14e274` prefix backends printk-s.

Source: `7.3/zram`. No `7.4/zram` branch exists, but the 7.4 release tree
contains the same code (verified in `drivers/block/zram/zcomp.[ch]` and
`zram_drv.h`). This laptop swaps to zram, so the recompression and stream
splitting are the parts that affect interactive latency under memory pressure.

### 3.5 Display: VESA DSC and passive VRR

| SHA | Subject | Files | Source |
|---|---|---:|---|
| `83b9e3af772e` | drm/edid: rename VESA block parsing functions to more generic name | 1 | `vesa-dsc-bpp` |
| `ad2c7d42df98` | drm/edid: prepare for VESA vendor-specific data block extension | 1 | `vesa-dsc-bpp` |
| `47e220374c12` | drm/edid: MSO should only be used for non-eDP displays | 2 | `vesa-dsc-bpp` |
| `4bed87802c26` | drm/edid: parse DSC DPP passthru support flag for mode VII timings | 3 | `vesa-dsc-bpp` |
| `f349a87087cf` | drm/edid: for consistency, use mask everywhere for block rev parsing | 2 | `vesa-dsc-bpp` |
| `4d2f3fc3124d` | drm/edid: parse DRM VESA dsc bpp target | 3 | `vesa-dsc-bpp` |
| `e5610e31ebe7` | drm/amd: use fixed dsc bits-per-pixel from edid | 1 | `vesa-dsc-bpp` |
| `6841144d7623` | drm/amdgpu: dsc: Fix max_qp limits to match VESA DSC 1.1 spec | 2 | `vesa-dsc-bpp` |
| `b8e2f4da5338` | drm: Add passive_vrr properties for passive/desktop VRR | 7 | **adapted** (`7.3/hdmi`) |
| `58c4471c0e5d` | drm/amd/display: Use passive_vrr properties in amdgpu | 3 | **adapted** (`7.3/hdmi`) |
| `c4b8a8e3cb39` | drm/amd/display: Keep FreeSync for HF-VSDB VRR sinks in MCCS fallback | 1 | **adapted** (`7.3/hdmi`) |

DSC matters for the internal panel and for external high-resolution displays;
the VRR set fixes FreeSync when a sink advertises VRR only through HF-VSDB.
**The three passive_vrr commits are not in the CachyOS 7.4 release** — there is
no `7.4/hdmi` branch. They are unmaintained by CachyOS now: keep them, but
expect conflicts on every rebase.

### 3.6 Platform, PCI, USB, media

| SHA | Subject | Files | Source |
|---|---|---:|---|
| `0c8178231070` | ACPI: processor: Disable bus master check for AMD | 1 | `cachy` |
| `43173c288242` | Revert "iommu/amd: Force identity mode for selected GPUs only" | 1 | **adapted** (`7.3/amd-iommu`) — **retired 2026-11-07** |
| `d2d5365e8bcf` | drivers/pci: Add ACS override support | 3 | **adapted** (`cachy`) |
| `94a22e28af31` | USB: core: sanitize string descriptors against C0 control characters | 1 | `fixes` |
| `be05b8ffa389` | x86/cpu/amd: Don't clear RDSEED32 bit on znver5 | 1 | `fixes` |
| `d4a254457260` | drm/amd: Move AMD_PRIVATE_COLOR to Kconfig | 5 | `cachy` |
| `78156984b6ce` | Revert "drm/gud: validate TV mode names before creating enum property" | 1 | `fixes` |

The gud revert is load-bearing: with clang 22+ and `-O3` the upstream
"validate TV mode names" change trips `__read_overflow` in `memchr()`
(`drivers/gpu/drm/gud/gud_connector.c:402`) and the build dies. Reproduced on
this tree before the revert was picked.

`ACS override` is gated behind `CONFIG_PCI_ACS_OVERRIDE`, which this tree does
**not** enable; the patch is inert unless that symbol is turned on.

The iommu revert is gone as of the 2026-11-07 upstream merge: CachyOS never
carried it past 7.3 (`cf18d488df9d` is not an ancestor of `cachyos/7.4/cachy`),
and upstream reworked the same code for PerfOpt while fixing the NULL bridge
dereference the original commit introduced. See `out-of-tree-patches.md` §8.

### 3.7 Networking

| SHA | Subject | Files | Source |
|---|---|---:|---|
| `2d61cf95234f` | tcp/bbr3: add BBRv3 congestion control | 17 | `bbr3` |
| `4760b65d47cd` | bbr: Fix bbr name to bbr3 | 2 | `bbr3` |

Taken from `cachyos/7.4/bbr3`; the 7.3 variant conflicts heavily in
`include/net/tcp.h`. Selected with `CONFIG_TCP_CONG_BBR3=y`.

### 3.8 Kbuild

| SHA | Subject | Files | Source |
|---|---|---:|---|
| `5b3b6a95a6c5` | Kbuild: Enable LLVM machine pipeliner | 2 | **adapted** (`cachy`) |

Adaptation: the CachyOS version also carried an unrelated
`drivers/acpi/processor_idle.c` `#ifdef CONFIG_X86` fix (that fix lives in
`0c8178231070` here). Only the `-mllvm -enable-pipeliner` line was kept.

### 3.9 Picked from 7.4-rc1-1 on 2026-11-06

Applied after the sync, in this order. All eight were verified to compile
(`kernel/sched/`, `mm/`, `drivers/usb/host/` with the recipe config) before
being committed.

| SHA | Subject | Source | Why |
|---|---|---|---|
| `8937893e930f` | usb: pci-quirks: always assert xHCI OS ownership | `fixes` | xHC OS-owned semaphore was asserted only when BIOS-owned was set; timeout path could clear both. AMD xHCI here, so S3 USB/BT enumeration is the failure mode |
| `0960b7556ea8` | CACHY: Disable proactive compaction by default | `cachy` | `compaction_proactiveness = 0` under `CONFIG_CACHY` |
| `234f9ea38c34` | CACHY: Disable watermark boosting by default | `cachy` | `watermark_boost_factor = 0`, avoids the kswapd thrash livelock |
| `55f9c312293b` | CACHY: Enable background reclaim of hugepages | `cachy` | THP defrag default becomes `defer+madvise` |
| `86704c28d36b` | CACHY: mm/vmscan: Increase vm_swappiness to 100 | `cachy` | SSD + zram; anon and file reclaim cost the same |
| `8c72ecf33139` | mm: Increase VM_READAHEAD_PAGES to 256K | `cachy` | Not `CONFIG_CACHY`-gated; sequential read throughput |
| `1de4ea4a1959` | rust: Allow BTF generation with Rust + LTO | `cachy` | Our LTO + BTF config silently dropped `CONFIG_RUST`; now selectable |

The EEVDF tuning pick (`17c3dd7d8067`, applied as `80b6f557b5b7`) was **dropped
the same day**: this tree's scheduler is BORE, so the EEVDF block is compiled
out and the pick was dead code. Its `SCHED_NR_MIGRATE_BREAK 8` hunk went with
it. Seven picks remain.

---

## 4. Local deltas (2 commits, not CachyOS)

Per-patch detail, including the version-tracking and retirement rules, lives in
`docs/out-of-tree-patches.md` §3. Summary:

| SHA | Subject | Why |
|---|---|---|
| `299f0b100cd8` | media: i2c: max96717: fix bitfield overflow in pinconf setter | `~en_val` does not fit a 1-bit `field_prep()` mask; AutoFDO + ThinLTO constant-folds the helper and the build fails in `ld.lld`. Reproduced with the recipe's own `afdo.prof`. Submitted upstream separately. |
| `8e1a563686be` | tools: add local A/B measurement and kexec campaign harness | `tools/exp/` — the measurement rig and its recorded noise floor. |

---

## 5. How we differ from CachyOS 7.4-rc1-1

### 5.1 CachyOS dropped BORE and POC for 7.4; we still carry both

Verifiable in one command each:

```sh
git grep -c sched_bore cachyos-7.4-rc1-1 -- kernel/sched init/Kconfig   # no hit
git grep -c poc_selector_active cachyos-7.4-rc1-1 -- kernel/sched       # no hit
git log -1 d2ae21da42b6                                                 # their POC revert
```

Their 7.4 scheduler story is **tuned EEVDF** instead (section 6, pick 6):
base slice 700 us -> 400 us and migration cost 500 us -> 400 us under
`CONFIG_CACHY`. The POC revert commit carries no rationale beyond
"This reverts commit ...".

Consequences for us:

- Our POC is **on** by default. Two review-found bugs were fixed here
  (`rcu_dereference_all`, and skipping BORE's burst restart on
  `DEQUEUE_DELAYED`), but it is now a fork of an abandoned patch.
- **Decision taken 2026-11-06:** BORE is this tree's scheduler, kept in-tree and
  selected by config; POC stays on; CachyOS's EEVDF tuning is rejected. Under
  `CONFIG_SCHED_BORE=y` the EEVDF base-slice block does not compile, so carrying
  it would be dead code — the pick was dropped rather than kept for symmetry.
- Consequence: we diverge from CachyOS's scheduler direction deliberately. Their
  future scheduler work needs re-evaluating, not auto-picking.

### 5.2 Topics we carry that 7.4 no longer ships

| Topic | Status in 7.4-rc1-1 | Consequence |
|---|---|---|
| BORE (`sched`) | absent | patch-file based; rebase by hand |
| POC selector | reverted | see above |
| passive_vrr set (`hdmi`) | absent | unmaintained; conflicts expected |
| zram set | present in tree, no branch | keep re-picking from `7.3/zram` |

### 5.3 Where we are identical despite a patch-id mismatch

`git cherry` / patch-id reports these as missing even though the code is
present, because CachyOS rebased them:

| Commit | Why the patch-id differs |
|---|---|
| `drivers/pci: Add ACS override support` + fixup | we carry both; the fixup is patch-identical (`ec5b3f84dfb2` vs `b8f9f009f1f4`) |
| `iosched-7.3: update ADIOS to 3.3.0` | our `block/adios.c` is byte-identical to the 7.4 tree |
| `Kbuild: Enable LLVM machine pipeliner` | adapted (section 3.8) |
| `7.2-rc1-poc-selector-v2.6.3` | taken from 7.3; 7.4's copy was rebased, then reverted |

---

## 6. Gap analysis vs `cachyos-7.4-rc1-1` — what is worth picking

130 commits in the release, 38 patch-identical to ours, 92 not. Removing
T2/Apple (49), Samsung Galaxy Book audio (8), Intel-only (i915 x3, iwlwifi,
intel-pstate, Intel remapped NVMe), the tag-release and packaging commits
(7 + `resolve_btfids`), and the non-applicable compiler work (Zen4 ISA, GCC
SMS) and VHBA leaves a short list.

Apply status was tested with `git cherry-pick -n` in a scratch worktree at HEAD.
**All eight were subsequently applied on 2026-11-06** — section 3.9 lists the
resulting commits:

| # | Commit | Applies | Why it fits this machine |
|---|---|---|---|
| 1 | `c75839fb62b0` usb: pci-quirks: always assert xHCI OS ownership | **clean** | Generic USB-core correctness: the xHC OS-owned semaphore is asserted only when BIOS-owned is already set, and the timeout path can clear both. Reported against AMD xHCI on S3 resume. We have AMD xHCI (1022:151a/151e/151f), so S3 USB/BT enumeration is the failure mode it prevents |
| 2 | `96fa1f26e3c1` CACHY: Disable proactive compaction by default | **clean** | `compaction_proactiveness = 0` under `CONFIG_CACHY`; fewer background stalls on a 30 GiB laptop |
| 3 | `c94b131ec0ac` CACHY: Disable watermark boosting by default | **clean** | `watermark_boost_factor = 0`; avoids the kswapd page-thrashing livelock described in the commit |
| 4 | `6c797e2903fb` CACHY: Enable background reclaim of hugepages | **clean** | THP defrag default becomes `defer+madvise`, which is what an interactive desktop wants |
| 5 | `a1a49cb638f4` CACHY: mm/vmscan: Increase vm_swappiness to 100 | **clean** | SSD + zram: file and anon reclaim cost the same, so 100 is defensible |
| 6 | `efac028fc910` mm: Increase VM_READAHEAD_PAGES to 256K | **clean** | Not `CONFIG_CACHY`-gated; doubles readahead for LPDDR5x sequential I/O |
| 7 | `b632093480e9` rust: Allow BTF generation with Rust + LTO | **clean** | Only relevant if Rust drivers are wanted: our LTO + BTF config silently drops `CONFIG_RUST` today |

Rejected on evidence, not taste:

- `1d750e7ce49e` btusb 13d3:3625 — our radio is **13d3:3608**.
- `bd67d9617058` G920 force feedback — our HID is `046d:c548` (Logi Bolt).
- `56daa4d70a8c` iwlwifi, `cd1bc1e37297` Dell SOF quirk, i915 RC6 pair — wrong vendor.
- `4958dab4b8fc` Intel remapped NVMe — Apple T2.
- `707025e74d27` Zen4/x86_64 ISA flags, `8aa5c6dc0368` GCC SMS — we build with
  clang and `-march=native`.
- `de9ca7ed8e6a` intel-pstate, `9c5ac595d8a1` VHBA — no matching hardware.
- `d2ae21da42b6` POC revert — a decision (section 5.1), not a pick.

### 6.1 Resolution used for the EEVDF pick before it was dropped

CachyOS's `#ifdef CONFIG_CACHY` has to nest inside our
`#else /* !CONFIG_SCHED_BORE */` branch:

```c
#else /* !CONFIG_SCHED_BORE */
#ifdef CONFIG_CACHY
unsigned int sysctl_sched_base_slice			= 400000ULL;
static unsigned int normalized_sysctl_sched_base_slice	= 400000ULL;
#else
unsigned int sysctl_sched_base_slice			= 700000ULL;
static unsigned int normalized_sysctl_sched_base_slice	= 700000ULL;
#endif /* CONFIG_CACHY */
#endif /* CONFIG_SCHED_BORE */

#ifdef CONFIG_CACHY
__read_mostly unsigned int sysctl_sched_migration_cost	= 400000UL;
#else
__read_mostly unsigned int sysctl_sched_migration_cost	= 500000UL;
#endif /* CONFIG_CACHY */
```

Verified: `kernel/sched/` compiles clean with `CONFIG_CACHY=y`, BORE off and
BORE on.

---

## 7. Runbook

### 7.1 Regenerate the gap analysis

```sh
cd ~/Projects/linux-catchya
git fetch --no-tags upstream master
git fetch --no-tags cachyos 'refs/heads/*:refs/remotes/cachyos/*'
git fetch --no-tags cachyos tag cachyos-<version>

# per-branch listing
git log --no-merges --format='%h %s' cachyos/7.4/cachy --not upstream/master

# what the release has that we do not (patch-id aware)
git cherry master cachyos-7.4-rc1-1 | grep '^+' | wc -l
```

`git cherry` is the cheap check and is patch-id based, so rebased commits show
as `+` even when the code is present. Confirm by hand before calling anything
missing (section 5.3).

### 7.2 Sync to a new CachyOS release

1. `tools/catchya-sync fetch` — lists new commits per topic branch.
2. Classify them with section 6's rubric: hardware match, generic fix,
   performance with a mechanism, or not applicable.
3. Cherry-pick the keepers with `-x`, resolving conflicts as in section 6.1.
4. Build: `make O=~/Workspace/exp/bt LLVM=1 LLVM_IAS=1 -j24`.
5. Boot smoke test in QEMU: `qemu-system-x86_64 -enable-kvm -cpu host -kernel
   bzImage -m 2048 -smp 4 -nographic -append "console=ttyS0 panic=-1"` — expect
   a VFS panic on the missing root and no oops before it.
6. `tools/catchya-sync rebase` when upstream moves; repeat 3-5.
7. Update this book: add rows in section 3, adjust sections 2 and 6.

### 7.3 Tools that pay for themselves

| Task | Command |
|---|---|
| Provenance of a commit | `git log -1 --format=%B <sha>` then read the "cherry picked from" line |
| Is a CachyOS patch already here? | `git cherry master <cachy-ref>` |
| Does a pick apply? | `git cherry-pick -n <sha>` in a scratch worktree |
| Which topic touched a file? | `git log --oneline cachyos/7.4/cachy -- <path>` |
| Compare trees directly | `git diff HEAD cachyos-7.4-rc1-1 -- <path>` |

---

## 8. Gotchas

1. **`cachyos/7.4/base` does not exist** — it is deleted after each release.
   The tag (`cachyos-7.4-rc1-1`) is the reference, and its merge commits name
   the topic branches.
2. **Topic branches are per-release and pruned.** `7.4/sched`, `7.4/hdmi` and
   `7.4/zram` are absent; absence in the branch list does not mean the code was
   dropped — check the tree.
3. **`7.4/fixes` is not "all fixes".** Scheduler and mm tweaks live in
   `7.4/cachy`; the split is CachyOS's packaging convenience, not taxonomy.
4. **Patch-id equality is a weak signal.** Six of our commits are functional
   duplicates that `git cherry` reports as missing (section 5.3).
5. **`-mprefer-vector-width` is 256 in the recipe** (mobile Zen 5 has a 256-bit
   AVX-512 datapath); do not "fix" it back to 512.
6. **The gud revert is load-bearing for clang builds.** Dropping it to get
   closer to upstream breaks the build, not just a test.
7. **AutoFDO + ThinLTO finds latent bugs.** It turned the max96717 bitfield
   overflow into a hard link error; expect more of these as profiles tighten.
8. **Upstream moves.** The fork's master merged torvalds master at
   `c463b80881fb` and again at `d48723711bae` (2026-11-07); rebase onto the
   newest merge before comparing against a new release, as was done for the
   eight picks in section 3.9.
9. **The eight picks own defaults, not just code.** `vm.swappiness = 100`,
   `watermark_boost_factor = 0`, `compaction_proactiveness = 0`, THP
   `defer+madvise` and 256 KiB readahead all change runtime
   behaviour under `CONFIG_CACHY=y`. The recipe then turns
   `CONFIG_THP_ASYNC_DEFRAG` on, which moves the THP default from
   `defer+madvise` to plain `defer` — `always [defer] defer+madvise` is what a
   booted package reads back. Reverting a "bad kernel" report starts here, and
   each is still overridable at runtime through sysfs/sysctl.
