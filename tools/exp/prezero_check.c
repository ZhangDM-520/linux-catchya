// SPDX-License-Identifier: GPL-2.0
/*
 * prezero_check - assert that anonymous pages handed out by the allocator are
 * still zero when CONFIG_PREZERO_PAGES (or init_on_alloc) says they must be.
 *
 * The failure this catches is a page that sat on a per-CPU "known zero" list
 * while something wrote to it: the allocator then skips the clear and userspace
 * reads stale bytes. The probe therefore dirties memory, releases it, and
 * immediately allocates fresh anonymous memory, checking every page.
 *
 *   prezero_check [--size MiB] [--rounds N]
 *
 * Pass: exit 0, "ok" on stdout. Fail: exit 1 and the first offending offset.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/mman.h>

#define PAGE 4096UL

static int check_zero(const unsigned char *p, size_t size, unsigned long round)
{
	for (size_t off = 0; off < size; off += PAGE) {
		if (p[off] || p[off + 1] || p[off + PAGE / 2] ||
		    p[off + PAGE - 1]) {
			fprintf(stderr,
				"FAIL round %lu: page at +0x%zx is not zero "
				"(first bytes %02x %02x %02x %02x)\n",
				round, off, p[off], p[off + 1],
				p[off + PAGE / 2], p[off + PAGE - 1]);
			return -1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	size_t size = 64UL << 20;
	static const struct option opts[] = {
		{ "size",   required_argument, NULL, 's' },
		{ "rounds", required_argument, NULL, 'r' },
		{ NULL, 0, NULL, 0 },
	};
	int rounds = 50, opt;
	unsigned long checked = 0;

	while ((opt = getopt_long(argc, argv, "s:r:", opts, NULL)) != -1) {
		switch (opt) {
		case 's':
			size = strtoul(optarg, NULL, 0) << 20;
			break;
		case 'r':
			rounds = atoi(optarg);
			break;
		default:
			fprintf(stderr, "usage: %s [--size MiB] [--rounds N]\n",
				argv[0]);
			return 2;
		}
	}
	size &= ~(PAGE - 1);
	if (!size)
		return 2;

	for (int r = 0; r < rounds; r++) {
		unsigned char *dirty = mmap(NULL, size, PROT_READ | PROT_WRITE,
					    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		unsigned char *fresh;

		if (dirty == MAP_FAILED) {
			perror("mmap dirty");
			return 2;
		}
		madvise(dirty, size, MADV_NOHUGEPAGE);
		memset(dirty, 0xaa, size);
		munmap(dirty, size);

		/* Let this CPU idle: the idle loop pre-zeroes freed pages. */
		usleep(20000);

		fresh = mmap(NULL, size, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (fresh == MAP_FAILED) {
			perror("mmap fresh");
			return 2;
		}
		madvise(fresh, size, MADV_NOHUGEPAGE);
		/* Reading is enough to fault the page in; do not write first,
		 * or the write would mask the stale bytes being checked for.
		 */
		if (check_zero(fresh, size, r))
			return 1;
		checked += size / PAGE;
		munmap(fresh, size);
	}

	printf("ok: %lu pages checked over %d rounds\n", checked, rounds);
	return 0;
}
