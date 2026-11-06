// SPDX-License-Identifier: GPL-2.0
/*
 * vmprobe - PID 1 for the mm validation VM.
 *
 * Boots as init, mounts proc/sys, then runs the two checks the mm campaign
 * needs inside the kernel under test:
 *
 *   1. zero invariant: dirty pages, release them, reallocate, verify zero.
 *      A stale byte here means a page was handed out of a "known zero" list
 *      after something had written to it.
 *   2. anonymous fault cost, 4 KiB and 2 MiB.
 *   3. the policy the patches set (THP defrag mode, MGLRU TTL) and the
 *      pre-zeroing counters from debugfs, so the run says what was active.
 *
 * Prints one line per result to the console and powers off. Exit code is not
 * observable from QEMU, so the lines are the contract: "PASS"/"FAIL",
 * "fault order0 ..." / "fault thp ...", "sysfs ...", "prezero ...".
 *
 * Build: cc -O2 -static -o init vmprobe.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define PAGE 4096UL

static unsigned long long now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long long)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static int cmp_u64(const void *a, const void *b)
{
	unsigned long long x = *(const unsigned long long *)a;
	unsigned long long y = *(const unsigned long long *)b;

	return x < y ? -1 : x > y;
}

static int zero_check(unsigned long size, int rounds)
{
	unsigned long checked = 0;
	int r;

	for (r = 0; r < rounds; r++) {
		unsigned char *dirty = mmap(NULL, size, PROT_READ | PROT_WRITE,
					    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		unsigned char *fresh;

		if (dirty == MAP_FAILED)
			return -1;
		madvise(dirty, size, MADV_NOHUGEPAGE);
		memset(dirty, 0xaa, size);
		munmap(dirty, size);

		/*
		 * Idle here: on a CPU with nothing to run, the idle loop
		 * pre-zeroes a batch of these just-freed pages, and the
		 * allocation below is served from that list.
		 */
		usleep(20000);

		fresh = mmap(NULL, size, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (fresh == MAP_FAILED)
			return -1;
		madvise(fresh, size, MADV_NOHUGEPAGE);
		for (unsigned long off = 0; off < size; off += PAGE) {
			if (fresh[off] || fresh[off + 1] ||
			    fresh[off + PAGE / 2] || fresh[off + PAGE - 1]) {
				printf("FAIL zero round %d at +0x%lx: "
				       "%02x %02x %02x %02x\n", r, off,
				       fresh[off], fresh[off + 1],
				       fresh[off + PAGE / 2],
				       fresh[off + PAGE - 1]);
				return -1;
			}
			checked++;
		}
		munmap(fresh, size);
	}
	printf("PASS zero invariant: %lu pages over %d rounds\n", checked, rounds);
	return 0;
}

static void fault_bench(const char *label, unsigned long size, int iters,
			int huge)
{
	static unsigned long long s[1 << 20];
	unsigned long n = 0;
	unsigned long long sum = 0;
	unsigned long stride = huge ? (2UL << 20) : PAGE;
	int it;

	for (it = 0; it < iters; it++) {
		char *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		if (p == MAP_FAILED)
			return;
		madvise(p, size, huge ? MADV_HUGEPAGE : MADV_NOHUGEPAGE);
		for (unsigned long off = 0; off < size; off += stride) {
			unsigned long long a, b;

			/* userspace: no ARRAY_SIZE() */
			if (n == sizeof(s) / sizeof(s[0]))
				break;
			a = now_ns();
			p[off] = 0x5a;
			b = now_ns();
			s[n++] = b - a;
		}
		munmap(p, size);
		/* Let this CPU reach the idle loop, where pages are pre-zeroed. */
		usleep(5000);
	}
	if (!n)
		return;
	qsort(s, n, sizeof(s[0]), cmp_u64);
	for (unsigned long i = 0; i < n; i++)
		sum += s[i];
	printf("fault %s faults=%lu mean_ns=%llu p50=%llu p99=%llu\n",
	       label, n, sum / n, s[n / 2], s[(n * 99) / 100]);
}

static void print_prezero_stats(void)
{
	char line[128];
	FILE *f = fopen("/debug/prezero_pages", "r");

	if (!f) {
		printf("prezero unavailable\n");
		return;
	}
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = 0;
		printf("prezero %s\n", line);
	}
	fclose(f);
}

static void print_sysfs(const char *label, const char *path)
{
	char line[256];
	FILE *f = fopen(path, "r");

	if (!f) {
		printf("sysfs %s unavailable\n", label);
		return;
	}
	if (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = 0;
		printf("sysfs %s %s\n", label, line);
	}
	fclose(f);
}

static void print_policy(void)
{
	print_sysfs("thp_defrag", "/sys/kernel/mm/transparent_hugepage/defrag");
	print_sysfs("thp_enabled", "/sys/kernel/mm/transparent_hugepage/enabled");
	print_sysfs("lru_gen_min_ttl_ms", "/sys/kernel/mm/lru_gen/min_ttl_ms");
	print_sysfs("lru_gen_enabled", "/sys/kernel/mm/lru_gen/enabled");
	print_sysfs("swappiness", "/proc/sys/vm/swappiness");
}

int main(void)
{
	mkdir("/proc", 0555);
	mkdir("/sys", 0555);
	mkdir("/dev", 0755);
	mkdir("/debug", 0755);
	mount("proc", "/proc", "proc", 0, NULL);
	mount("sysfs", "/sys", "sysfs", 0, NULL);
	mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
	mount("debugfs", "/debug", "debugfs", 0, NULL);

	printf("vmprobe: kernel %s\n", "under test");
	fflush(stdout);

	print_policy();
	fflush(stdout);

	zero_check(32UL << 20, 20);
	fflush(stdout);
	print_prezero_stats();

	fault_bench("order0", 32UL << 20, 10, 0);
	fflush(stdout);

	fault_bench("thp", 32UL << 20, 10, 1);
	fflush(stdout);
	print_prezero_stats();

	printf("vmprobe: done\n");
	fflush(stdout);

	sync();
	reboot(RB_POWER_OFF);
	for (;;)
		pause();
	return 0;
}
