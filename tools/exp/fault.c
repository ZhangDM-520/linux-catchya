// SPDX-License-Identifier: GPL-2.0
/*
 * fault - anonymous page-fault latency probe for the mm A/B campaigns.
 *
 * Maps an anonymous region, faults one byte per page while timing each
 * fault, then unmaps it and repeats. Peak RSS is bounded by --size, so the
 * numbers are about fault cost (allocation + zeroing + page-table work),
 * not about how much memory the machine has.
 *
 *   fault --size 32 --iters 20 [--thp=4k|madvise] [--touch=1] [--json]
 *
 * Reported per run:
 *   mean/p50/p90/p99 ns per fault, faults/s, CPU time per iteration, and the
 *   AnonHugePages delta read from /proc/self/smaps_rollup (a zero delta for
 *   --thp=madvise means the kernel refused to give a hugepage, which is the
 *   THP fallback path this campaign cares about).
 *
 * Build: cc -O2 -o fault fault.c
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>

#define PAGE 4096UL
#define MAX_SAMPLES (1UL << 22)

static unsigned long long *samples;
static unsigned long nsamples;

static inline unsigned long long now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long long)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static unsigned long smaps_anonhuge_kb(void)
{
	char line[256];
	unsigned long kb = 0;
	FILE *f = fopen("/proc/self/smaps_rollup", "r");

	if (!f)
		return 0;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "AnonHugePages: %lu kB", &kb) == 1)
			break;
	}
	fclose(f);
	return kb;
}

static int cmp_u64(const void *a, const void *b)
{
	unsigned long long x = *(const unsigned long long *)a;
	unsigned long long y = *(const unsigned long long *)b;

	return x < y ? -1 : x > y;
}

static unsigned long long pct(double p)
{
	unsigned long idx;

	if (!nsamples)
		return 0;
	idx = (unsigned long)(p * (double)(nsamples - 1));
	return samples[idx];
}

int main(int argc, char **argv)
{
	static const struct option opts[] = {
		{ "size",  required_argument, NULL, 's' },
		{ "iters", required_argument, NULL, 'i' },
		{ "thp",   required_argument, NULL, 't' },
		{ "touch", required_argument, NULL, 'T' },
		{ "dontneed", no_argument,    NULL, 'd' },
		{ "json",  no_argument,       NULL, 'j' },
		{ NULL, 0, NULL, 0 },
	};
	size_t size = 32UL << 20;
	int iters = 20, thp_madv = 0, json = 0;
	int dontneed = 0;
	int touch = 1, opt, it;
	unsigned long long total_faults = 0;
	unsigned long huge_kb = 0;
	struct rusage ru0, ru1;
	char *map = NULL;
	int fd = -1;

	while ((opt = getopt_long(argc, argv, "s:i:t:T:dj", opts, NULL)) != -1) {
		switch (opt) {
		case 's':
			size = strtoul(optarg, NULL, 0) << 20;
			break;
		case 'i':
			iters = atoi(optarg);
			break;
		case 't':
			if (!strcmp(optarg, "madvise"))
				thp_madv = 1;
			break;
		case 'T':
			touch = atoi(optarg);
			break;
		case 'd':
			dontneed = 1;
			break;
		case 'j':
			json = 1;
			break;
		default:
			fprintf(stderr, "usage: %s [--size MiB] [--iters N] "
				"[--thp=4k|madvise] [--touch N] [--dontneed] "
				"[--json]\n",
				argv[0]);
			return 2;
		}
	}

	size &= ~(PAGE - 1);
	if (!size || touch < 1 || touch > (int)PAGE)
		touch = 1;

	samples = malloc(MAX_SAMPLES * sizeof(*samples));
	if (!samples)
		return 1;

	if (json) {
		/* keep stdout for the report */
		fd = dup(1);
		freopen("/dev/null", "w", stdout);
	}

	getrusage(RUSAGE_SELF, &ru0);

	for (it = 0; it < iters; it++) {
		char *p;

		if (dontneed) {
			/*
			 * One mapping for the whole run: MADV_DONTNEED
			 * discards the pages without tearing the VMA down,
			 * so the next touch faults cold pages back in and
			 * the mmap/munmap cost stays out of the loop.
			 */
			if (!it) {
				map = mmap(NULL, size, PROT_READ | PROT_WRITE,
					   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
				if (map == MAP_FAILED) {
					perror("mmap");
					return 1;
				}
				madvise(map, size, thp_madv ? MADV_HUGEPAGE :
							      MADV_NOHUGEPAGE);
			}
			p = map;
			madvise(p, size, MADV_DONTNEED);
		} else {
			p = mmap(NULL, size, PROT_READ | PROT_WRITE,
				 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (p == MAP_FAILED) {
				perror("mmap");
				return 1;
			}
			/*
			 * The default must be base pages: the recipe builds
			 * with TRANSPARENT_HUGEPAGE_ALWAYS, so without
			 * MADV_NOHUGEPAGE a 32 MiB mapping becomes sixteen
			 * 2 MiB folios and the loop measures present-page
			 * stores, not faults.
			 */
			if (thp_madv)
				madvise(p, size, MADV_HUGEPAGE);
			else
				madvise(p, size, MADV_NOHUGEPAGE);
		}

		const unsigned long stride = thp_madv ? (2UL << 20) : PAGE;

		for (unsigned long off = 0; off < size; off += stride) {
			unsigned long long t0, t1;

			if (nsamples == MAX_SAMPLES)
				break;
			t0 = now_ns();
			memset(p + off, 0x5a, touch);
			t1 = now_ns();
			samples[nsamples++] = t1 - t0;
			total_faults++;
		}
		{
			unsigned long kb = smaps_anonhuge_kb();

			if (kb > huge_kb)
				huge_kb = kb;
		}
		if (!dontneed)
			munmap(p, size);
	}
	if (dontneed)
		munmap(map, size);

	getrusage(RUSAGE_SELF, &ru1);

	{
		unsigned long long mean = 0;
		long minflt;
		double cpu_ms;

		minflt = (ru1.ru_minflt - ru0.ru_minflt) / (iters ? iters : 1);

		for (unsigned long i = 0; i < nsamples; i++)
			mean += samples[i];
		mean = nsamples ? mean / nsamples : 0;

		qsort(samples, nsamples, sizeof(*samples), cmp_u64);

		cpu_ms = (double)(ru1.ru_utime.tv_sec - ru0.ru_utime.tv_sec) * 1e3 +
			 (double)(ru1.ru_utime.tv_usec - ru0.ru_utime.tv_usec) / 1e3 +
			 (double)(ru1.ru_stime.tv_sec - ru0.ru_stime.tv_sec) * 1e3 +
			 (double)(ru1.ru_stime.tv_usec - ru0.ru_stime.tv_usec) / 1e3;

		if (json) {
			FILE *out = fdopen(fd, "w");

			if (out) {
				fprintf(out, "{\"faults\": %llu, "
					"\"mean_ns\": %llu, \"p50_ns\": %llu, "
					"\"p90_ns\": %llu, \"p99_ns\": %llu, "
					"\"cpu_ms_per_iter\": %.3f, "
					"\"minflt_per_iter\": %ld, "
					"\"anonhuge_kb\": %lu}\n",
					total_faults, mean,
					pct(0.50), pct(0.90), pct(0.99),
					cpu_ms / (iters ? iters : 1),
					minflt, huge_kb);
				fclose(out);
			}
		} else {
			printf("faults=%llu mean_ns=%llu p50=%llu p90=%llu "
			       "p99=%llu cpu_ms/iter=%.3f minflt/iter=%ld "
			       "anonhuge_kb=%lu\n",
			       total_faults, mean, pct(0.50), pct(0.90),
			       pct(0.99), cpu_ms / (iters ? iters : 1),
			       minflt, huge_kb);
		}
	}

	free(samples);
	return 0;
}
