// SPDX-License-Identifier: GPL-2.0
/*
 * futex_ping - wake-to-run latency for a futex pair, and where the waiter ran.
 *
 * The waiter blocks in FUTEX_WAIT_PRIVATE; the waker parks it, flips the word
 * and calls FUTEX_WAKE_PRIVATE. The latency is measured on the waiter, from a
 * timestamp the waker publishes immediately before the wake syscall:
 *
 *   latency = clock_gettime(after FUTEX_WAIT returns) - wake_ts
 *
 * So it includes the syscall, the scheduler decision and the switch - exactly
 * what a cache-affine wake policy would move. Each sample also records the CPU
 * the waiter resumed on, so the caller can count cross-LLC wakes by reading
 * /sys/devices/system/cpu/cpuN/cache/index3/shared_cpu_list.
 *
 * The round handshake uses two eventfds, so the measured futex word is only
 * touched by the operation under test.
 *
 *   futex_ping [--iters N] [--waker-cpu N] [--waiter-cpu N] [--json]
 *   futex_ping --init [--hotplug CPU]      # QEMU boot probe (vmtest.sh)
 *
 * As PID 1 (vmtest.sh) it mounts the pseudo-filesystems, clamps the CPU ids
 * to the online set, hotplugs the highest-numbered CPU when there are more
 * than two (the topology-edge check for a wake path that reads LLC masks),
 * prints PASS/FAIL lines and powers off. --init forces that mode and
 * --hotplug CPU names the CPU to bounce instead.
 *
 * Samples where the waiter never slept (the wake raced ahead of the park) are
 * counted, dropped and reported as "raced".
 *
 * In the default mode this is a measurement tool and makes no pass/fail
 * claim; only --init prints PASS lines.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/futex.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

/* Give the waiter time to enter the kernel before the waker fires. */
#define PARK_SETTLE_US 50

struct shared {
	atomic_int word;		/* the futex word, shared across fork */
	atomic_ullong wake_ts;
	atomic_uint waiter_cpu;
	atomic_uint waker_cpu;
	atomic_uint llc_hits;		/* wakeups that resumed in the waker's LLC */
	cpu_set_t llc;			/* CPUs sharing the waker's L3 */
	atomic_int llc_known;
	unsigned long long samples[];	/* ULLONG_MAX = raced, dropped */
};

static int p_ready[2];
static int p_ack[2];

static inline unsigned long long now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long long)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/*
 * Non-private operations on purpose: the word lives in MAP_SHARED memory and
 * the two processes have different mm_structs, so a _PRIVATE futex would not
 * match across the fork.
 */
static int futex_wait(int *uaddr, int val)
{
	return syscall(SYS_futex, uaddr, FUTEX_WAIT, val, NULL, NULL, 0);
}

static int futex_wake(int *uaddr, int nr)
{
	return syscall(SYS_futex, uaddr, FUTEX_WAKE, nr, NULL, NULL, 0);
}

/*
 * One byte per round on each pipe. An eventfd coalesces two writes into one
 * read, which desynchronises a strict ping-pong.
 */
static void ef_write(int fd)
{
	char one = 1;

	if (write(fd, &one, 1) != 1)
		_exit(1);
}

static void ef_read(int fd)
{
	char one;

	while (read(fd, &one, 1) != 1) {
		if (errno != EINTR)
			_exit(1);
	}
}

static void pin_to(int cpu)
{
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	if (sched_setaffinity(0, sizeof(set), &set))
		fprintf(stderr, "sched_setaffinity(%d): %s\n", cpu,
			strerror(errno));
}

/*
 * The LLC of a CPU, from the cache topology. "0-3,8-11" in sysfs is parsed
 * into a cpu_set_t; an absent index3 leaves the set empty, and the caller
 * reports the count as unknown rather than guessing.
 */
static int read_llc_mask(int cpu, cpu_set_t *set)
{
	char path[96], buf[256];
	char *tok, *save = NULL;
	int fd, n;

	CPU_ZERO(set);
	snprintf(path, sizeof(path),
		 "/sys/devices/system/cpu/cpu%d/cache/index3/shared_cpu_list",
		 cpu);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = '\0';
	buf[strcspn(buf, "\n")] = '\0';

	for (tok = strtok_r(buf, ",", &save); tok;
	     tok = strtok_r(NULL, ",", &save)) {
		char *dash = strchr(tok, '-');
		int lo, hi;

		if (dash) {
			*dash = '\0';
			lo = atoi(tok);
			hi = atoi(dash + 1);
		} else {
			lo = hi = atoi(tok);
		}
		if (lo < 0 || hi >= CPU_SETSIZE)
			continue;
		for (int i = lo; i <= hi; i++)
			CPU_SET(i, set);
	}
	return CPU_COUNT(set) ? 0 : -1;
}

static void unpin_all(void)
{
	long n = sysconf(_SC_NPROCESSORS_ONLN);
	cpu_set_t set;

	CPU_ZERO(&set);
	for (long i = 0; i < n; i++)
		CPU_SET((int)i, &set);
	if (sched_setaffinity(0, sizeof(set), &set))
		fprintf(stderr, "sched_setaffinity(all): %s\n", strerror(errno));
}

static int cmp_u64(const void *a, const void *b)
{
	unsigned long long x = *(const unsigned long long *)a;
	unsigned long long y = *(const unsigned long long *)b;

	return x < y ? -1 : x > y;
}

static unsigned long long pct(unsigned long long *s, unsigned long n, double p)
{
	if (!n)
		return 0;
	return s[(unsigned long)(p * (double)(n - 1))];
}

static void waiter(struct shared *sh, long iters, int cpu, int unpin)
{
	pin_to(cpu);

	for (long i = 0; i < iters; i++) {
		int slept = 0;

		atomic_store_explicit(&sh->word, 0, memory_order_release);
		ef_write(p_ready[1]);
		while (atomic_load_explicit(&sh->word, memory_order_acquire) == 0) {
			if (!futex_wait((int *)&sh->word, 0))
				slept = 1;
			else if (errno != EAGAIN)
				_exit(1);
		}

		if (slept) {
			unsigned long long t = now_ns();
			unsigned int resumed = (unsigned)sched_getcpu();

			sh->samples[i] = t -
				atomic_load_explicit(&sh->wake_ts,
						     memory_order_acquire);
			atomic_store(&sh->waiter_cpu, resumed);
			if (atomic_load(&sh->llc_known) &&
			    CPU_ISSET((int)resumed, &sh->llc))
				atomic_fetch_add(&sh->llc_hits, 1);
		} else {
			sh->samples[i] = ULLONG_MAX;
		}
		/*
		 * Let the scheduler place the wakee after the first park: a
		 * pinned waiter cannot show a wake-placement change at all.
		 */
		if (unpin && i == 0)
			unpin_all();
		ef_write(p_ack[1]);
	}
	_exit(0);
}

static int mount_init_fs(void)
{
	mkdir("/proc", 0555);
	mkdir("/sys", 0555);
	mkdir("/dev", 0755);
	if (mount("proc", "/proc", "proc", 0, NULL) ||
	    mount("sysfs", "/sys", "sysfs", 0, NULL) ||
	    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL))
		return -1;
	return 0;
}

/* 0 = written, 1 = CPU absent, -1 = the write failed */
static int hotplug(int cpu, int on)
{
	char path[64];
	int fd, ret;

	snprintf(path, sizeof(path),
		 "/sys/devices/system/cpu/cpu%d/online", cpu);
	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return errno == ENOENT ? 1 : -1;
	ret = write(fd, on ? "1" : "0", 1) == 1 ? 0 : -1;
	close(fd);
	return ret;
}

static int sysctl_read_int(const char *path)
{
	char buf[16];
	int fd, n;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = '\0';
	return atoi(buf);
}

static int sysctl_write_str(const char *path, const char *val)
{
	int fd, ret;

	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	ret = write(fd, val, strlen(val)) == (ssize_t)strlen(val) ? 0 : -1;
	close(fd);
	return ret;
}

/*
 * The hint is a static key, so turning it off and on at runtime is the part
 * that can corrupt state if the branch is used from a context the key does
 * not cover. Exercise the toggle before measuring.
 */
static void affine_sysctl_check(void)
{
	const char *path = "/proc/sys/kernel/sched_futex_affine";
	int v = sysctl_read_int(path);

	if (v < 0) {
		printf("SKIP sched_futex_affine absent\n");
		return;
	}
	printf("PASS futex sysctl present (%d)\n", v);

	if (sysctl_write_str(path, "0") || sysctl_read_int(path) != 0) {
		printf("FAIL futex sysctl off\n");
		return;
	}
	printf("PASS futex sysctl off\n");

	if (sysctl_write_str(path, "1") || sysctl_read_int(path) != 1) {
		printf("FAIL futex sysctl on\n");
		return;
	}
	printf("PASS futex sysctl on\n");
}

static int measure(long iters, int waker_cpu, int waiter_cpu, int json,
		   const char *label, int unpin)
{
	struct shared *sh;
	pid_t pid;
	unsigned long long raced = 0;
	unsigned long long n = 0;
	unsigned long long sum = 0;

	sh = mmap(NULL, sizeof(*sh) + (size_t)iters * sizeof(sh->samples[0]),
		  PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (sh == MAP_FAILED)
		return -1;

	if (pipe(p_ready) || pipe(p_ack))
		return -1;

	atomic_init(&sh->word, 0);
	atomic_init(&sh->wake_ts, 0);
	atomic_init(&sh->llc_hits, 0);
	atomic_init(&sh->llc_known, read_llc_mask(waker_cpu, &sh->llc) == 0);
	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0)
		waiter(sh, iters, waiter_cpu, unpin);

	pin_to(waker_cpu);
	for (long i = 0; i < iters; i++) {
		ef_read(p_ready[0]);
		usleep(PARK_SETTLE_US);
		atomic_store_explicit(&sh->wake_ts, now_ns(),
				      memory_order_release);
		atomic_store_explicit(&sh->word, 1, memory_order_release);
		futex_wake((int *)&sh->word, 1);
		atomic_store(&sh->waker_cpu, (unsigned)sched_getcpu());
		ef_read(p_ack[0]);
	}
	waitpid(pid, NULL, 0);

	for (long i = 0; i < iters; i++) {
		if (sh->samples[i] == ULLONG_MAX)
			raced++;
		else
			n++;
	}
	if (!n)
		return -1;

	{
		unsigned long long *v = malloc(n * sizeof(*v));
		unsigned long long k = 0;

		if (!v)
			return -1;
		for (long i = 0; i < iters; i++)
			if (sh->samples[i] != ULLONG_MAX)
				v[k++] = sh->samples[i];
		qsort(v, n, sizeof(*v), cmp_u64);
		for (unsigned long i = 0; i < n; i++)
			sum += v[i];

		if (json) {
			printf("%s{\"samples\": %llu, \"raced\": %llu, "
			       "\"wake_ns_mean\": %llu, \"p50_ns\": %llu, "
			       "\"p90_ns\": %llu, \"p99_ns\": %llu, "
			       "\"llc_hits\": %u, \"waiter_cpu\": %u, "
			       "\"waker_cpu\": %u}\n",
			       label, n, raced, sum / n, pct(v, n, 0.50),
			       pct(v, n, 0.90), pct(v, n, 0.99),
			       atomic_load(&sh->llc_hits),
			       atomic_load(&sh->waiter_cpu),
			       atomic_load(&sh->waker_cpu));
		} else {
			printf("%swake_ns: samples=%llu raced=%llu mean=%llu "
			       "p50=%llu p90=%llu p99=%llu llc_hits=%u/%llu "
			       "llc=%s waiter_cpu=%u waker_cpu=%u\n",
			       label, n, raced, sum / n, pct(v, n, 0.50),
			       pct(v, n, 0.90), pct(v, n, 0.99),
			       atomic_load(&sh->llc_hits), n,
			       atomic_load(&sh->llc_known) ? "known" : "?",
			       atomic_load(&sh->waiter_cpu),
			       atomic_load(&sh->waker_cpu));
		}
		free(v);
	}
	munmap(sh, sizeof(*sh) + (size_t)iters * sizeof(sh->samples[0]));
	return 0;
}

int main(int argc, char **argv)
{
	static const struct option opts[] = {
		{ "iters",      required_argument, NULL, 'i' },
		{ "waker-cpu",  required_argument, NULL, 'w' },
		{ "waiter-cpu", required_argument, NULL, 'W' },
		{ "json",       no_argument,       NULL, 'j' },
		{ "init",       no_argument,       NULL, 'I' },
		{ "hotplug",    required_argument, NULL, 'H' },
		{ NULL, 0, NULL, 0 },
	};
	long iters = 2000;
	int waker_cpu = 0;
	int waiter_cpu = 1;
	int hotplug_cpu = -1;
	int waiter_set = 0;
	int json = 0;
	int init = 0;
	int opt;
	int ret;

	while ((opt = getopt_long(argc, argv, "i:w:W:jIH:", opts, NULL)) != -1) {
		switch (opt) {
		case 'i':
			iters = atol(optarg);
			break;
		case 'w':
			waker_cpu = atoi(optarg);
			break;
		case 'W':
			waiter_cpu = atoi(optarg);
			waiter_set = 1;
			break;
		case 'j':
			json = 1;
			break;
		case 'I':
			init = 1;
			break;
		case 'H':
			hotplug_cpu = atoi(optarg);
			break;
		default:
			fprintf(stderr, "usage: %s [--iters N] [--waker-cpu N] "
				"[--waiter-cpu N] [--json] [--init] "
				"[--hotplug CPU]\n", argv[0]);
			return 2;
		}
	}
	if (iters < 1)
		return 2;

	if (getpid() == 1) {
		long ncpu = sysconf(_SC_NPROCESSORS_ONLN);

		init = 1;
		if (waker_cpu >= ncpu)
			waker_cpu = 0;
		if (!waiter_set && ncpu > 1)
			waiter_cpu = ncpu - 1;
		if (waiter_cpu >= ncpu)
			waiter_cpu = ncpu > 1 ? 1 : 0;
		if (hotplug_cpu < 0 && ncpu > 2)
			hotplug_cpu = (int)ncpu - 1;
	}

	if (init && mount_init_fs())
		return 1;
	if (init)
		affine_sysctl_check();

	if (hotplug_cpu >= 0) {
		int hp = hotplug(hotplug_cpu, 0);

		if (hp > 0) {
			printf("SKIP hotplug cpu%d absent\n", hotplug_cpu);
		} else if (hp < 0) {
			printf("FAIL hotplug cpu%d offline\n", hotplug_cpu);
		} else {
			printf("PASS hotplug cpu%d offline\n", hotplug_cpu);
			if (measure(200, 0, 1, 0, "hotplug ", 0))
				printf("FAIL hotplug ping\n");
			else
				printf("PASS hotplug ping\n");
			if (hotplug(hotplug_cpu, 1))
				printf("FAIL hotplug cpu%d online\n",
				       hotplug_cpu);
			else
				printf("PASS hotplug cpu%d online\n",
				       hotplug_cpu);
		}
	}

	if (init) {
		/*
		 * Same pair, same iterations, hint off then on: the two lines
		 * are the A/B, and llc_hits is the placement half of it.
		 */
		sysctl_write_str("/proc/sys/kernel/sched_futex_affine", "0");
		if (measure(iters / 2 ?: 1, waker_cpu, waiter_cpu, 0,
			    "affine_off ", 1))
			return 1;
		sysctl_write_str("/proc/sys/kernel/sched_futex_affine", "1");
		if (measure(iters / 2 ?: 1, waker_cpu, waiter_cpu, 0,
			    "affine_on ", 1))
			return 1;
		ret = 0;
	} else {
		ret = measure(iters, waker_cpu, waiter_cpu, json, "", 0);
	}
	if (ret)
		return 1;

	if (init) {
		printf("PASS futex ping\n");
		printf("futex_ping: done\n");
		fflush(stdout);
		/* Let the emulated UART drain before the poweroff message. */
		sleep(1);
		sync();
		reboot(RB_POWER_OFF);
		for (;;)
			pause();
	}
	return 0;
}
