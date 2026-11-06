# The catchya out-of-tree patch book

Reference for every patch this tree carries that **torvalds/linux does not have**.
It exists so that each one can be re-applied, re-measured, and retired without
repeating the archaeology, and so that new out-of-tree optimisation imports get
the same treatment from the day they land.

Companion book: `docs/cachyos-stack.md` covers the commits taken *from* CachyOS
releases. This one covers everything else — including CachyOS **patch files**
their own tree no longer ships, which is how BORE and POC ended up here.

Last reviewed: 2026-11-07, against `master` = `cc8482fde096`.

---

## 1. Scope: what "out-of-tree" means here

"Out-of-tree" = not merged in Linus's tree. Three consequences decide what
belongs in this book and what does not:

| Origin | Where it is tracked |
|---|---|
| torvalds/linux, merged | Neither book — it is upstream |
| A CachyOS release tree we cherry-pick from | `docs/cachyos-stack.md` |
| CachyOS **patch files** (`kernel-patches`), vendor trees, mailing-list series, distro patch sets, our own fixes | **This book** |

The third row is the point: a patch can be "CachyOS" by authorship and still be
out-of-tree in practice. BORE and POC are the current examples — CachyOS dropped
both from their 7.4 kernel branch, yet `kernel-patches` still publishes the BORE
patch and nothing maintains POC at all.

**Rule of thumb:** if `git log --grep "cherry picked from"` does not explain a
commit, and the CachyOS book's commit lists do not contain it, it belongs here.

---

## 2. Ledger

Status values: **carried** (in `master`), **trialling** (a topic branch,
built and booted, not yet on `master`), **build-time** (applied by the
recipe), **retired** (removed, kept only as a note in section 8).

| # | Patch | Status | Lives as | Upstream path | Conflict risk |
|---|---|---|---|---|---|
| 1 | BORE scheduler | carried | `c0e7962295db` (14 files) | none — CachyOS dropped it for 7.4 | **high** — `kernel/sched/fair.c` |
| 2 | POC idle selector | carried | `1a3b10033f63` (8 files) | none — reverted by CachyOS | **high** — `select_idle_sibling` |
| 3 | max96717 bitfield fix | carried | `299f0b100cd8` (1 file) | submitted | low |
| 4 | AutoFDO Rust flags | carried | `55ee2831893d` (1 file) | should go upstream | low |
| 5 | `dkms-clang.patch` | build-time | recipe `dkms-clang.patch` | none (deliberate) | low |
| 6 | NVIDIA open patches | build-time | `kernel-patches/<major>/misc/nvidia/*` | none | **high** — version-scoped |
| 7 | THP async defrag | trialling | `90c8b34dd9df` (3 files) | none — a policy default, not submitted | low — `mm/huge_memory.c` defrag default, `should_compact_retry()` |
| 8 | Idle pre-zeroing | trialling | `8b143fc6fce5` (7 files) | none | **medium** — `mm/page_alloc.c` alloc and free hot paths |

Not a patch, kept for completeness: `8e1a563686be` `tools/exp/` (A/B and kexec
campaign harness), extended by `61e1870a62df` with the fault-latency and QEMU
correctness probes used by rows 7 and 8. It touches no kernel code, so it
cannot conflict; it is the measurement gate every future import in section 5
must pass.

Also not a patch: the MGLRU half of the same import request. It resolved to
"nothing to import" — see the candidate register in §5.4.

---

## 3. Carried in-tree

### 3.1 BORE — `CONFIG_SCHED_BORE`

| Fact | Value |
|---|---|
| Our commit | `c0e7962295db` "bore-cachy" (author date 2026-09-22) |
| Upstream patch | `kernel-patches/master/7.4/sched/0001-bore-cachy.patch`, commit `d84748c3cdf6`, Piotr Gorski |
| Blast radius | 14 files, +947 −11 |
| Archived in a CachyOS release? | No. Their 7.4 branch has no `sched_bore`; the patch file is the only maintenance source |
| Config | `SCHED_BORE` default `y` in `init/Kconfig`; the recipe asserts and sets it |

**Storage moved on 2026-11-06:** the recipe used to fetch this patch file into
`source[]` and choose it with `_cpusched=bore`. The PKGBUILD rewrite removed the
scheduler variant entirely (commit `8ba1713` in the recipe repo), so BORE now
exists only as the commit above and the recipe only asserts that the tree still
defines `config SCHED_BORE`.

**Measured delta from the published patch** (2026-11-07): applying the 7.4 patch
with `git apply --3way` to the tree at `c0e7962295db^` touches 14 files, 13
clean, and conflicts in exactly one place:

| Site | Why it conflicts | Our resolution |
|---|---|---|
| `kernel/sched/fair.c`, `sysctl_sched_base_slice` | The patch is generated against CachyOS's **400 µs** base slice; our tree carries upstream's **700 µs** because the EEVDF tuning pick was dropped | Took the patch's `#ifdef CONFIG_SCHED_BORE` arm (`nsecs_per_tick`), kept upstream's value in the `#else` arm |

With that one resolution the tree is **identical** to `c0e7962295db` — so the
commit is the published patch plus exactly one semantic edit:

| Site | Change | Why |
|---|---|---|
| `kernel/sched/fair.c`, `dequeue_entity` | `if ((flags & DEQUEUE_SLEEP) && !(flags & DEQUEUE_DELAYED))` instead of `if (flags & DEQUEUE_SLEEP)` | 7.4's delayed dequeue (`abc158c82ae5`) defers the actual dequeue; without the guard BORE restarts the burst on a dequeue that is not a sleep, corrupting the burst/penalty accounting |

Also note `place_entity()` is rewritten twice over the patch: the `vslice`
computation moves below the futex-reparent early-return and is shifted for
`ENQUEUE_WAKEUP|ENQUEUE_INITIAL`. That part applied cleanly but is inside the
hottest EEVDF path, so it is the first thing to re-read on a scheduler change.

**Re-verify at a bump** (assumes BORE is still a commit; see section 7.1 for the
general form):

```sh
git worktree add --detach ../borechk c0e7962295db^
cd ../borechk
curl -sO https://raw.githubusercontent.com/CachyOS/kernel-patches/master/<major>/sched/0001-bore-cachy.patch
git apply --3way 0001-bore-cachy.patch      # expect rc=1 + one conflict in fair.c
# resolve fair.c, then:
git diff c0e7962295db -- .                  # only the DEQUEUE_DELAYED guard may remain
```

**Retire when:** BORE is upstream (not scheduled), or the tree stops selecting
it. If CachyOS's patch file stops tracking kernel mainline closely enough, the
fallback source is Masahito Suzuki's own BORE repository, which is the author's
tree rather than a packaging patch.

### 3.2 POC selector — `CONFIG_SCHED_POC_SELECTOR`

| Fact | Value |
|---|---|
| Our commit | `1a3b10033f63` "7.2-rc1-poc-selector-v2.6.3" |
| Source | CachyOS/linux `661ebafd`, in `7.3/cachy`; written originally against 7.2 |
| Blast radius | 8 files, +2365 −40; `kernel/sched/poc_selector.c` alone is 2038 lines |
| CachyOS status | reverted for 7.4 by `d2ae21da42b6` with no rationale beyond "This reverts" |
| Patch file | **none.** `7.3/sched/` and `7.4/sched/` ship only `0001-bore-cachy.patch` and `0001-prjc-cachy.patch` — verified 2026-11-07 |
| Config | `SCHED_POC_SELECTOR` default `y` in `init/Kconfig`; recipe sets it |

Also unpatched by the recipe since 2026-11-06: `SCHED_POC_SELECTOR` is asserted
by prepare() and written with `-e`, never fetched.

`kernel/sched/poc_selector.c` is **byte-identical** to `661ebafd` (2038 lines,
empty `diff`). All local work is at the call sites:

| File | Adaptation |
|---|---|
| `kernel/sched/fair.c` | `select_idle_sibling()`, `__update_idle_core()`, `select_task_rq_fair()` — routing through the POC fast path, and `goto select_smt_priority` so upstream's asymmetric-SMT work (`c8fc4136fd3c`) still runs |
| `include/linux/sched/topology.h` | per-LLC bitmap state in `struct sched_domain_shared` (`poc_llc_members`, `poc_cpu_base`, `poc_affinity_shift`, `poc_fast_eligible`, `poc_cluster_valid`, and the SMT pair) |
| `kernel/sched/sched.h` | per-task hook state and prototypes (+118 lines) |
| `kernel/sched/idle.c`, `kernel/sched/ext/ext.c`, `kernel/sched/topology.c` | idle-entry and sched_ext bookkeeping |

**Ownership note:** this is now a fork of an abandoned patch. If it breaks, the
only reference implementation is `661ebafd` on `7.3/cachy` — a branch their 7.4
release has reverted, not carried forward. Two review-found bugs are already
fixed here (RCU dereference discipline via `rcu_dereference_all`, and the BORE
`DEQUEUE_DELAYED` interaction in 3.1). Treat the 2038-line file as ours.

**Retire when:** the fast path is measurably not worth its conflict surface. It
is a pure idle-CPU-search optimisation; `tools/exp/` can settle that question
without a bootcamp. See `docs/cachyos-stack.md` §5.1 for the decision to keep it.

### 3.3 max96717 bitfield fix — `299f0b100cd8`

Out-of-tree because it is ours and only just submitted. One file
(`drivers/media/i2c/max96717.c`), one operator: `~en_val` → `!en_val`. A plain
build masks the high bits and is fine; AutoFDO + ThinLTO constant-folds the
helper and trips `__BF_FIELD_CHECK_MASK()` in `include/linux/bitfield.h`, which
the linker reports as `field_prep: value too large for the field`. This patch is
the difference between the recipe's `_autofdo=yes` build working and not.

**Retire when** the upstream submission lands and `git cherry` reports it as
present.

### 3.4 AutoFDO Rust flags — `55ee2831893d`

Out-of-tree because mainline has not caught the toolchain drift yet. One file
(`scripts/Makefile.autofdo`), two renames:

| Flag | Old spelling | New spelling | Gate |
|---|---|---|---|
| Rust sample profile | `-Zprofile-sample-use` | `-Cprofile-sample-use` | `rustc-min-version,110000` (rust-lang/rust#155942, Rust 1.100.0) |
| LLVM function splitting | `-Cllvm-args=-split-machine-functions` | `-Cllvm-args=-function-splitting=all` | `rustc-option` probe — rustc bundles its own LLVM, so the rustc version does not describe the LLVM version |

Without it, `make prepare` dies at `RUSTC L rust/core.o` with
`error: unknown unstable option: profile-sample-use`; with only the first fix it
dies at the LLVM argument. Both were measured on rustc 1.101.0-nightly /
LLVM 24.0.0; see `.logs/action-2026-11-06-autofdo-rust-flags.md`.

**Retire when** the equivalent lands in `scripts/Makefile.autofdo` upstream
(mainline already fixed the linker half in `2b358510d5cc`).

### 3.5 THP async defrag — `CONFIG_THP_ASYNC_DEFRAG`

| Fact | Value |
|---|---|
| Our commit | `90c8b34dd9df`, branch `mm/thp-async-defrag`, 3 files, +35 −1 |
| Provenance | written here (2026-11-07). No vendor patch to diff against: `defer` has been a runtime value of the defrag knob since it was added, and the promoted-order case in `should_compact_retry()` came in with `defrag_mode` |
| Touch surface | `mm/huge_memory.c` (compiled default, 1 hunk), `mm/page_alloc.c` (`should_compact_retry()`, 1 hunk), `mm/Kconfig` |
| Upstream status | none, and not submitted: this is a policy default, not a bug fix |
| Measurement | booted (2026-11-07), plain and `DEBUG_VM` builds: the defrag attribute reads `always [defer] defer+madvise madvise never`. Not measured: the `thp_fault_fallback` rate and the fault P99 on bare metal — this laptop resolves ~5 % on timings, so the gate has to be a counter comparison |
| Storage | commit on a topic branch; nothing to fetch at build time |
| Retirement | the recipe stops selecting it, or a counter campaign shows the fallback cost is not worth the latency |

Two deltas, both keyed off the Kconfig:

| Delta | Site | Effect |
|---|---|---|
| Default becomes `defer`, not `defer+madvise` | `transparent_hugepage_flags`, `mm/huge_memory.c:61-77` | `vma_thp_gfp_mask()` returns `GFP_TRANSHUGE_LIGHT \| __GFP_KSWAPD_RECLAIM` for every VMA, `MADV_HUGEPAGE` included, so a fault never enters direct reclaim or synchronous compaction |
| No priority escalation for promoted orders | `should_compact_retry()`, `mm/page_alloc.c` | the promoted `pageblock_order` request falls back to the `ALLOC_NOFRAGMENT` retry instead of retrying compaction at increasing priority |

`THP_FAULT_FALLBACK` accounting is untouched, so a lost hugepage is always a
visible counter rather than a silent policy change. `defrag` stays writable at
runtime; the Kconfig only changes the compiled default.

**Retire when** the `thp_fault_fallback` rate rises beyond noise on a quiet
machine with no fault-latency win, or the recipe stops selecting the option.

### 3.6 Idle pre-zeroing — `CONFIG_PREZERO_PAGES`

| Fact | Value |
|---|---|
| Our commit | `8b143fc6fce5`, branch `mm/prezero-pages`, 7 files, +336 −20 |
| Provenance | written here (2026-11-06/07). No mainline or vendor equivalent exists; `folio_zero_user()` is the closest prior art and encodes the opposite half of the idea (keep the faulting page hot) |
| Touch surface | `mm/page_alloc.c` (`rmqueue_pcplist()`, `post_alloc_hook()`, `prep_new_page()`, `free_pcppages_bulk()`, `per_cpu_pages_init()`, `prezero_pcp()`, debugfs counters), `include/linux/mmzone.h` (`struct per_cpu_pages`), `include/linux/mm.h`, `mm/page_alloc.h`, `mm/compaction.c` (call sites), `kernel/sched/idle.c` (`do_idle()` hook), `mm/Kconfig` |
| Upstream status | none; this is a new mechanism, not a backport |
| Measurement | QEMU boot (2026-11-07): `filled` 326 → 430, `used` 318 → 407 over one probe run; zero invariant over 163840 pages; the same boot on a `DEBUG_VM` build is clean; `init_on_free=1` and `page_poison=1` both report `filled 0` and pass the invariant. Not measured: per-fault CPU cycles on bare metal |
| Storage | commit on a topic branch |
| Retirement | the per-fault cycle gate cannot beat the noise floor, or mainline grows an equivalent |

**The invariant.** A page is zero if and only if it sits on a
`pcp->zero_lists[mt]` entry. Nothing else may put a page there.
`free_pcppages_bulk()` splices the zero lists back into the normal lists
before draining, so CPU hotplug, zone drain and list overflow all degrade to
stock behaviour. Runtime guards: `init_on_free` and `page_poisoning` both
disable the feature (each deliberately leaves a free page in a state that is
not zero); Kconfig refuses `DEBUG_PAGEALLOC` and `KMSAN`.

**Designs refused** (recorded so they are not re-proposed): zeroing in
`free_unref_page()` (charges the freeing task, often kswapd or an exiting
process), zeroing inside the buddy allocator (holds `zone->lock`, and the page
is re-verified after a merge anyway), and a per-page "zeroed" flag (no free
`page` flag is safe to reuse in 7.4; list membership is free). Non-temporal
stores are refused too: they bypass the cache, so the first user write to the
page takes an extra DRAM fetch — the opposite of what `folio_zero_user()`
achieves.

**Retire when** the cycle gate on a quiet machine rejects it, or the allocator
grows an equivalent mechanism.

**Rebase surface.** Every symbol below changed shape during the 7.3→7.4 work,
and two of them are silent corruption if resolved the obvious way:

| Symbol in an older tree | In this tree | Rule at a bump |
|---|---|---|
| `kernel_init_free_pages(page, order)` | `clear_highpages_kasan_tagged()` in `mm/page_alloc.c` | use it — it resets KASAN tags and carries the s390 caveat |
| `clear_page(addr)` | `clear_pages(addr, npages)` with ERMS alternatives in `arch/x86/include/asm/page_64.h` | keep the per-page call `clear_highpages_kasan_tagged(page, 1)` |
| `prep_new_page(page, order, gfp, alloc_flags)` | same four arguments | gains `prezeroed`; all callers are inside `mm/`, so the compiler finds them |
| `rmqueue_pcplist(zone, order, mt, alloc_flags)` | also takes `preferred_zone` | the zero-list lookup must stay inside the pcp trylock, before the normal list |
| `struct per_cpu_pages` | `lists[NR_PCP_LISTS]`, `high_min`/`high_max`/`alloc_factor` | append fields, never reorder |
| `free_pcppages_bulk()` | takes `pindex` and drains round-robin | the splice must target `pcp->lists[order_to_pindex(mt, 0)]`, **not** `pcp->lists[mt]`: the index is `mt * (PAGE_ALLOC_COSTLY_ORDER + 1) + order`, so `lists[mt]` is a different order's list for every `mt > 0` |
| `do_idle()` | `kernel/sched/idle.c`, compiled into `build_policy.o` | hook before `local_irq_disable()`; the stub in `include/linux/mm.h` keeps `PREZERO_PAGES=n` builds identical |

---

## 4. Build-time patches (applied by the recipe)

These never enter git history, so nothing else can see them. They are listed
here because they break silently at a version bump.

### 4.1 `dkms-clang.patch` — local, always applied

From CachyOS (Eric Naim, 2026-02-23); 2 files, −6 lines. It removes four
`-Werror` flags that make out-of-tree (DKMS) modules built with clang fail:

| File | Flag removed |
|---|---|
| `scripts/Makefile.clang` | `-Werror=unknown-warning-option`, `-Werror=unused-command-line-argument` |
| `scripts/Makefile.warn` | `-Werror=strict-prototypes`, `-Werror=incompatible-pointer-types` |

Not upstream material: upstream wants those warnings fatal for in-tree code, and
the patch cannot distinguish in-tree from DKMS. **Verify at a bump:** the patch
touches two small Makefiles that change rarely; a `patch -Np1 --dry-run` in the
extracted tree is enough, and the recipe already fails loudly if it stops
applying.

### 4.2 NVIDIA open modules — version-scoped, opt-in

Applied only with `_build_nvidia_open=yes`; fetched from
`_patchsource = https://raw.githubusercontent.com/CachyOS/kernel-patches/master/<major>`
where `<major>` is derived from `pkgver`. Changing the kernel major therefore
changes the patch set, and **a missing file is a download failure, not a build
warning**.

**Drift found 2026-11-07 (unfixed — recipe owns `source[]`):**

| Version | Files in `misc/nvidia/` | What each does |
|---|---|---|
| 7.3 | `0001`, `0002`, `0003`, `0004` | `0001` DSC RC parameter tables vs VESA DSC; `0002` Bigscreen Beyond VR headset DP WAR; `0003` adapt `dmem_cgroup_register_region()` to the `struct dmem_cgroup_init` API added in mainline `e11640b9cea4`; `0004` open sysfs config through the userspace root (`scoped_with_init_fs()`), without which runtime D3 reads "Disabled by default" |
| 7.4 | `0001`, `0002`, `0004`, `0005` | `0003` is dropped — that API is simply part of 7.4 now. `0005` is the actual 7.4 build fix: `VM_SPECIAL` removal (`72a993449cb0`), `is_vm_hugetlb_page()` → `vma_is_hugetlb()` (`c911e7ebebf5`), and the fbdev helper move to `drm/clients/drm_fbdev_helper.h` (`6ccc5b5fc1d3`) |

The PKGBUILD still pins `0001`–`0003` for every major. On a 7.4 build (what
`pkgver=7.4.rc1` resolves to today) `0003` 404s at download time, and `0004`
plus `0005` — the patch that actually makes the module compile on 7.4 — are
never applied.

**Check before enabling the knob:**

```sh
major=7.4
for f in $(curl -s https://api.github.com/repos/CachyOS/kernel-patches/contents/$major/misc/nvidia | jq -r '.[].name'); do
    printf '%s -> ' "$f"
    curl -s -o /dev/null -w '%{http_code}\n' \
        "https://raw.githubusercontent.com/CachyOS/kernel-patches/master/$major/misc/nvidia/$f"
done
```

---

## 5. Intake protocol for new optimisation imports

Every out-of-tree optimisation costs a rebase conflict forever. The point of
this section is to make that cost explicit **before** the patch lands, so the
ledger never fills with things nobody can evaluate.

### 5.1 The six fields

A patch may enter `master` only when all six are written down in the ledger:

1. **Provenance** — repo, commit id, author, date, and the kernel version it was
   written against (not the version it "should work on").
2. **Touch surface** — `git show --stat`, plus the functions inside any file
   above ~50 changed lines. `kernel/sched/`, `mm/`, `block/` and `drivers/gpu/`
   are the expensive ones.
3. **Upstream status** — merged (then just wait for a rebase), submitted with a
   link, or abandoned. An abandoned patch is a permanent maintenance liability:
   say so in the ledger row.
4. **Measurement** — the `tools/exp/` noise floor on this laptop is ~5 % on
   timings, so a patch either moves a deterministic counter
   (`instructions:k` is stable to ~0.4 %) or it does not get to claim a win.
   "Feels snappier" is not a result.
5. **Retirement condition** — the observable event that lets the patch go
   (upstream merge, config flip, measurement below noise).
6. **Storage** — commit on `master`, or patch file in the recipe. Choose one;
   never both (see 7.4).

### 5.2 Preferred order of import

1. **Upstream equivalent already merged in a later release** — never import the
   out-of-tree copy; rebase instead.
2. **Upstream equivalent submitted** — import only if the tree is unusable
   without it, and record the submission link so retirement is a `git cherry`.
3. **Vendor/distro patch file with a stable URL and a version-scoped directory**
   — cheap to track, cheap to drop. This is the BORE/NVIDIA model.
4. **A bare commit in someone's branch** — most expensive: no patch file, no
   version discipline, and it can be force-pushed away at any time. POC is in
   this class.

### 5.3 Ledger row template

```markdown
| N | <name> | <status> | <commit or patch file> | <upstream status: none/submitted/merged> | <conflict risk + site> |
```

with a section below it carrying the six fields. Keep the table sorted by
status (carried, build-time, retired) so the top of the file is the current
cost surface.

### 5.4 Candidate register

Where an import waits before it earns a row in section 2. Status flow:
`watching` → `trialling` (a local branch, never `master`) → `carried` or
`rejected`.

| Candidate | Source | Status | Measurement | Notes |
|---|---|---|---|---|
| MGLRU aggressive tuning / backports | mainline `LRU_GEN*`, tuned by `CONFIG_CACHY` | **rejected — nothing to import** | in-tree and already on: `LRU_GEN=y`, `LRU_GEN_ENABLED=y`, caps `0x0007` (`CORE\|MM_WALK\|NONLEAF_YOUNG`), `swappiness` 100 | No MGLRU commit landed upstream after our base (`git log c463b80881fb..upstream/master --grep lru_gen` is empty), so there is no backport to take. The one real finding is a unit bug: `lru_gen_min_ttl` is 100 **jiffies**, so the protection window silently follows `_HZ_ticks` — 334 ms at the validation build's HZ=300, 166 ms for the HZ=600 package. It is a 2-line change (`HZ / 6`) *if* a measurement ever shows the HZ-scaled window is wrong; no measurement does, so no patch is carried |
| THP async defrag | this tree | **trialling** | §3.5 | `CONFIG_THP_ASYNC_DEFRAG` |
| Idle page pre-zeroing | this tree | **trialling** | §3.6 | `CONFIG_PREZERO_PAGES` |

A candidate leaves `trialling` with exactly one of: a deterministic-counter win
above noise, an upstream merge (then it is not an import at all), or a recorded
rejection with the number that killed it.

### 5.5 Landing ritual

Three steps, in this order, in one branch:

1. Import the patch (`git am -3` for a series, `git cherry-pick -x` for a single
   commit) so provenance stays in the commit object.
2. Add the six fields and the ledger row to this book.
3. Build the touched subsystem with the recipe's config and record the command
   in the row. `tools/exp/` measurement is separate and can follow — but a patch
   does not reach `master` without both.

---

## 6. Where out-of-tree optimisation patches come from

Watchlist, not a shopping list. Each entry notes *what to compare against*
before importing anything from it.

| Source | What it tends to carry | How to check it cheaply |
|---|---|---|
| CachyOS `kernel-patches` | per-major directories with scheduler, mm, BBR3, NVIDIA patches | `contents/<major>` listing; a 404 means the topic left that release |
| CachyOS release trees | the assembled set | `docs/cachyos-stack.md` gap analysis (§6) |
| BORE upstream (Masahito Suzuki) | the scheduler itself, independent of packaging | compare `SCHED_BORE_VERSION` in `include/linux/sched/bore.h` |
| sched_ext schedulers (scx) | userspace-loadable policies — no patch cost at all | prefer these over in-tree scheduler patches when a policy will do |
| Distribution patch sets (Arch, Fedora, Clear Linux, OpenWrt) | build fixes and vendor enablement | usually already in mainline by the time we look |
| Vendor trees (AMD, Intel, NVIDIA) | platform enablement for new silicon | relevant only when the hardware is in this laptop |

Decision records from this repo: `docs/cachyos-stack.md` §5 (why BORE and POC
stay), §6 (gap analysis and rejected picks), and `.logs/exp/REPORT.md` (what the
measurement campaign concluded).

---

## 7. Version-tracking runbook

### 7.1 Prove a carried commit equals its upstream patch

The technique that produced section 3.1, and the one to use on every bump:

```sh
# 1. parent of the patch commit, in a throwaway worktree
git worktree add --detach /tmp/chk <patch-commit>^
cd /tmp/chk

# 2. the upstream patch, for the *current* major
curl -s -o /tmp/p.patch https://raw.githubusercontent.com/CachyOS/kernel-patches/master/<major>/<path>

# 3. apply with 3-way; check the exit code directly - piping masks it
git apply --3way /tmp/p.patch; echo "rc=$?"
grep -c '^<<<<<<<' <file>          # conflicts, and where

# 4. resolve, stage, then diff the whole tree against the carried commit
git add -A
git diff --cached <patch-commit> --stat
```

An empty diff means the carried commit is the published patch verbatim. A
non-empty diff is the local adaptation list, and it belongs in this book — that
is the cheapest possible documentation of a rebase decision.

### 7.2 Rebase an in-tree patch onto a new base

```sh
git rebase --onto <new-base> <old-base> master   # rerere is on
```

Conflict rules learned here:

- **`kernel/sched/fair.c` conflicts are semantic, not cosmetic.** The BORE patch
  assumed a 400 µs base slice; ours is upstream's 700 µs. Resolve toward the
  BORE `#ifdef` arm and leave upstream's value in the `#else` arm.
- **Never resolve a scheduler conflict by deleting the `#ifdef`.** BORE and POC
  are both compiled out in some configs; the arm exists so the tree still builds
  with `SCHED_BORE=n`.
- **Re-read `place_entity()` and `dequeue_entity()` after any EEVDF change.**
  They are where 7.4's single-runqueue and delayed-dequeue reworks meet BORE.

### 7.3 Build-time patch applicability

```sh
cd <extracted tree>
for p in ../*.patch; do
    printf '%s: ' "$p"; patch -Np1 --dry-run < "$p" >/dev/null 2>&1 && echo ok || echo FAIL
done
```

`patch -N` returns 1 for an already-applied patch, so a FAIL here can mean "the
fix is in the tree" — check `git log --grep` before re-adding it.

### 7.4 One writer per patch

A fix lives either in `master` or in the recipe's `source[]`, never both. On
2026-11-06 the AutoFDO Rust fix existed in both for a few hours and prepare()
died with `Reversed (or previously applied) patch detected`. When a fix lands
in-tree, delete the patch file **and** its `b2sums` entry in the same change.

---

## 8. Retired patches — do not re-add

| Patch | Retired | Why |
|---|---|---|
| `0001-muqss-cachy.patch` | 2026-11-05 (`7b252a4`) | MuQSS is unmaintained and not rebasable; BORE is the tree's scheduler. The `_cpusched=muqss` arm was removed 2026-11-06 |
| `0001-autofdo-function-splitting-ld-option.patch` | 2026-11-05 (`7b252a4`) | Upstream `2b358510d5cc` landed the `ld-option` probe in-tree |
| `0001-autofdo-rust-flags.patch` | 2026-11-06 | Superseded same day by in-tree `55ee2831893d`; the recipe patch would now fail as already-applied |
| `0001-klflags-user-link-flags.patch` | 2026-11-02 (rewrite) | The `KLDFLAGS` channel existed only to forward `makepkg.conf` flags; the recipe now bakes `_kernel_cc_flags` and never lets makepkg flags reach kbuild. The file is still tracked in the recipe repo but wired out of `source[]` — safe to delete |

---

## 9. Gotchas

- **`git apply` and `patch` disagree about "already applied".** `git apply --3way`
  merges twice-applied patches without complaint in some cases; `patch -N` refuses.
  When in doubt, diff the resulting tree against the commit (7.1).
- **Piping hides the exit status.** `git apply --3way p | head` reports `head`'s
  status. Check `$?` on the bare command, or use `PIPESTATUS`.
- **A patch-id cannot see an adapted patch.** BORE and POC are not cherry-picks,
  so `git cherry` will report them as missing on every rebase. `docs/cachyos-stack.md`
  §5.3 has the same trap for rebased CachyOS commits.
- **`_patchsource` is version-scoped, and a 404 surfaces at download time.** The
  recipe derives `<major>` from `pkgver`; bumping the kernel silently changes
  which NVIDIA patches exist (section 4.2).
- **`b2sums` is positional.** Any change to a patch file needs `updpkgsums`, and
  adding an always-applied source needs the sum at the matching index.
- **Kconfig defaults hide a lost patch.** `SCHED_BORE` and
  `SCHED_POC_SELECTOR` both default to `y` in `init/Kconfig`, so a tree that
  loses them builds happily with the stock scheduler. The recipe asserts both
  symbols in `init/Kconfig` before writing them — keep that assertion.
- **BORE defaults are per-HZ.** `nsecs_per_tick = 1000000000ULL / HZ` and
  `sysctl_sched_base_slice = nsecs_per_tick` mean the base slice follows the
  recipe's `_HZ_ticks` (600 Hz here). A slice measurement taken on a 250 Hz
  kernel is not comparable.
