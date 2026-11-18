# catchya — repo rules for AI work

`master` is a Linux kernel fork: torvalds/linux plus the CachyOS 7.4 desktop stack, BORE and a few local mm/sched/AMD changes. `README` is the project contract and `AGENTS.md` symlinks to it — read both before touching anything.

## Mandatory process

- Read `Documentation/process/coding-assistants.rst` before contributing.
- Trailers: `Assisted-by: LLM (human-reviewed)` only — never `Signed-off-by` (only a human certifies the DCO), never `Co-authored-by`.
- Never commit, push, tag, mail or reply unasked; state unbuilt/untested scope explicitly.
- Bug fixes: reproduce non-trivial bugs first, cite `Fixes: <12+ sha> ("subject")`, land a warning-free build plus `scripts/checkpatch.pl --strict`.
- GPL-2.0-only compatible, SPDX headers per `license-rules.rst`; recipients via `scripts/get_maintainer.pl`; security triage per `threat-model.rst`.
- One logical, bisectable change per commit on area branches (`sched/…`, `mm/…`, `amd/…`, `sync/…`); `master` must build and boot before an experiment branch is cut.

## Where truth lives (read, do not restate)

- `docs/out-of-tree-patches.md`: ledger rows §2, intake protocol §5, version-tracking runbook §7, retired patches §8, gotchas §9, intake verdicts §10–11.
- `docs/cachyos-stack.md`: the 58 commits §3, local deltas §4, divergence from the release §5, gap analysis §6, runbook §7.
- Versions: `Makefile` is build truth, `git describe` and `catchya/*` tags are fork position — never force them to agree.

## Repo mechanics

- Remotes: `upstream` = torvalds/linux, `cachyos` = CachyOS/linux, `origin` = this fork.
- `.gitignore` rule `.*` keeps `.github/` and `.logs/` untracked; there is no CI. This file is tracked by an explicit `git add -f`.
- This checkout holds in-tree build outputs: one writer — never concurrent `bench.sh`, `vmtest.sh` or `mrproper`. Prefer `O=` builds; `make mrproper` first when an in-tree `.config` exists.

## Carried code (never "clean up" toward upstream)

- BORE: `kernel/sched/bore.c` + `fair.c`, `SCHED_BORE=y` — row 1. POC: `kernel/sched/poc_selector.c`, `#include`d by `fair.c` with no `obj-` line — row 2.
- Cache-affine futex wake: `futex_affine_cpu()` + `WF_FUTEX` in `fair.c`, `SCHED_FUTEX_AFFINE=n`, switchable at `/proc/sys/kernel/sched_futex_affine` — row 10, parked.
- THP deferred defrag: `mm/huge_memory.c` + `should_compact_retry()` — row 7. Idle pre-zeroing: `mm/page_alloc.c`, `include/linux/mm{,zone}.h`, `PREZERO_PAGES` — row 8.
- GFXOFF entry delay: `drivers/gpu/drm/amd/amdgpu/amdgpu_gfx.c`, `amdgpu.gfx_off_delay_ms` — row 9.
- BBR3: `net/ipv4/tcp_bbr3.c`, `TCP_CONG_BBR3=m` (never `=y`). AutoFDO/Propeller link flags: `scripts/Makefile.{autofdo,propeller}`; objtool — row 11.
- `max96717` `!en_val` fix now lives at `drivers/media/i2c/maxim-serdes/max96717.c`. `tools/exp/` is fork-owned: review it like kernel code.

## Measurement gate

- A change lands with a deterministic win or a recorded refusal; never on "feels snappier".
- Bars: `rps`/`build_wall` ≥ +1.0 %, `wakeup_p99`/`hackbench` ≤ −2.0 %, and ≥ 3 sigma, read from `.logs/exp/thresholds.json`; recalibrate per machine or `HZ` change, never remember a floor.
- Never cite `cyclictest` (outlier sigma) or QEMU timings as silicon results; sub-percent claims need `perf stat -e instructions:k`.
- `tools/exp/bench.sh --tag NAME`, `tools/exp/ab.sh --a A --b B`, `tools/exp/vmtest.sh`, `tools/exp/campaign.sh plan|dry-run|resume|init`.
- `ab.sh` locks `.logs/exp/.ab.lock`; `bench.sh` pins CPUs 0–11 and records governor/EPP/thermal state. Override tree and output paths with `EXP_DIR`/`BUILD_TREE`/`SRC`/`BUILD`; `vmtest.sh` also takes `KERNEL`/`MEM`/`SMP`/`TMO`/`INIT_SRC`/`EXPECT`/`DONE`/`WITH_MODULES`.

## Build, lint, test

- Built by the companion recipe, config `config-7.4.rc2-1-zeus`: clang + ThinLTO + AutoFDO/Propeller, HZ=600, PREEMPT_LAZY, `CONFIG_CACHY=y`. The recipe's carried-tree checks are implications, so renaming or dropping a symbol needs a recipe update in the same change.
- `scripts/config` bypasses Kconfig and silently drops renamed or gated symbols on `olddefconfig`; verify in the saved config or with `extract-ikconfig`.
- Lint: `scripts/checkpatch.pl --strict -g HEAD` (or `-f <file>`), `make C=1|W=1 <obj>`, `coccicheck MODE=report`, `rustfmtcheck`, `dt_binding_check`/`dtbs_check`, `scripts/kernel-doc -none`, `make htmldocs`.
- Tests: `./tools/testing/kunit/kunit.py run` (no tree `.kunitconfig`; uses `configs/default.config`), `make TARGETS=<t> kselftest`.
- Style: tabs, K&R, `/* */`, no unrelated reformats; see `coding-style.rst` and `submitting-patches.rst`.

## Traps (full list: ledger §9)

- `TCP_CONG_BBR=y` plus `TCP_CONG_BBR3=y` duplicates `__BTF_ID__func__bbr_*`; `resolve_btfids` fails with a message naming neither — keep BBR3 `=m`.
- `SCHED_BORE`/`SCHED_POC_SELECTOR` default to `y`, so a lost patch still builds; keep the recipe's symbol assertions.
- `git apply --3way` and `patch -N` disagree about "already applied"; piping hides `$?`. `git patch-id` cannot see an adapted patch — verify by worktree tree-diff against the commit.
- One writer per patch: tree or recipe `source[]`, never both; delete the patch and its positional `b2sums` entry together.
- BORE slice math and MGLRU `lru_gen_min_ttl` (100 jiffies) follow `HZ`; compare only at equal `HZ`.
- `CONFIG_CACHY` sets swappiness 100, watermark boost 0, compaction proactiveness 0, THP `defer`, 256 KiB readahead.
- Keep `-mprefer-vector-width=256` (Zen 5's 256-bit datapath) and the `gud` revert (clang build).
- ThinLTO/AutoFDO expose latent UB as link errors — fix the code, not the flag.
- Module BTF is relative to the kernel's base BTF: rebuild modules with the kernel, or `finit_module()` returns EINVAL.
- objtool rewrites objects in place and is not idempotent: use a fresh copy per run.
