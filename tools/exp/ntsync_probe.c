// SPDX-License-Identifier: GPL-2.0
/*
 * ntsync_probe - PID 1 for a QEMU boot that exercises /dev/ntsync.
 *
 * ntsync is in-tree (drivers/misc/ntsync.c) and built as a module. Booting a
 * kernel with this probe does three things a host-side smoke test cannot:
 *
 *   1. loads the module from the initramfs with finit_module() and reports
 *      whether the build actually links and initialises;
 *   2. runs the ioctl ABI Proton depends on - create semaphore, read it,
 *      release it, wait on it, create/read/unlock a mutex, create/set/reset
 *      an event - and checks the returned values;
 *   3. prints one "PASS ntsync <area>" line per area, so the boot log is the
 *      contract (exit codes are not observable from QEMU).
 *
 * Build: cc -O2 -static -o init ntsync_probe.c
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../../include/uapi/linux/ntsync.h"
#include "modload.h"

static unsigned long long now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long long)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static int sem_test(int dev)
{
	struct ntsync_sem_args a = { .count = 2, .max = 5 };
	uint32_t release = 1;
	int fd;

	fd = ioctl(dev, NTSYNC_IOC_CREATE_SEM, &a);
	if (fd < 0) {
		printf("FAIL ntsync sem create: %s\n", strerror(errno));
		return -1;
	}

	a.count = a.max = 0;
	if (ioctl(fd, NTSYNC_IOC_SEM_READ, &a) < 0 || a.count != 2 ||
	    a.max != 5) {
		printf("FAIL ntsync sem read: %u/%u\n", a.count, a.max);
		return -1;
	}

	if (ioctl(fd, NTSYNC_IOC_SEM_RELEASE, &release) < 0 || release != 2) {
		printf("FAIL ntsync sem release: prev=%u\n", release);
		return -1;
	}

	a.count = a.max = 0;
	if (ioctl(fd, NTSYNC_IOC_SEM_READ, &a) < 0 || a.count != 3) {
		printf("FAIL ntsync sem count after release: %u\n", a.count);
		return -1;
	}

	printf("PASS ntsync sem\n");
	return fd;
}

static int wait_test(int dev, int semfd)
{
	struct ntsync_sem_args before = { 0 }, after = { 0 };
	int fds[1] = { semfd };
	struct ntsync_wait_args w = {
		.timeout = 0,		/* absolute time 0: poll, do not sleep */
		.objs = (uint64_t)(uintptr_t)fds,
		.count = 1,
		.index = 0xffffffff,
		.flags = 0,
		.owner = (uint32_t)getpid(),
	};

	ioctl(semfd, NTSYNC_IOC_SEM_READ, &before);

	if (ioctl(dev, NTSYNC_IOC_WAIT_ANY, &w) < 0) {
		printf("FAIL ntsync wait_any: %s\n", strerror(errno));
		return -1;
	}
	if (w.index != 0) {
		printf("FAIL ntsync wait_any index: %u\n", w.index);
		return -1;
	}

	ioctl(semfd, NTSYNC_IOC_SEM_READ, &after);
	if (after.count + 1 != before.count) {
		printf("FAIL ntsync wait_any count: %u -> %u\n",
		       before.count, after.count);
		return -1;
	}

	printf("PASS ntsync wait\n");
	return 0;
}

static int mutex_test(int dev)
{
	struct ntsync_mutex_args a = { .owner = (uint32_t)getpid(), .count = 1 };
	int fd;

	fd = ioctl(dev, NTSYNC_IOC_CREATE_MUTEX, &a);
	if (fd < 0) {
		printf("FAIL ntsync mutex create: %s\n", strerror(errno));
		return -1;
	}

	a.owner = a.count = 0;
	if (ioctl(fd, NTSYNC_IOC_MUTEX_READ, &a) < 0 ||
	    a.owner != (uint32_t)getpid() || a.count != 1) {
		printf("FAIL ntsync mutex read: owner=%u count=%u\n",
		       a.owner, a.count);
		return -1;
	}

	a.owner = (uint32_t)getpid();
	a.count = 1;
	if (ioctl(fd, NTSYNC_IOC_MUTEX_UNLOCK, &a) < 0 || a.count != 1) {
		printf("FAIL ntsync mutex unlock: prev=%u\n", a.count);
		return -1;
	}

	a.owner = a.count = 0;
	if (ioctl(fd, NTSYNC_IOC_MUTEX_READ, &a) < 0 || a.owner != 0 ||
	    a.count != 0) {
		printf("FAIL ntsync mutex released state: owner=%u count=%u\n",
		       a.owner, a.count);
		return -1;
	}

	printf("PASS ntsync mutex\n");
	return fd;
}

static int event_test(int dev)
{
	struct ntsync_event_args a = { .manual = 1, .signaled = 0 };
	uint32_t prev = 0xdead;
	int fd;

	fd = ioctl(dev, NTSYNC_IOC_CREATE_EVENT, &a);
	if (fd < 0) {
		printf("FAIL ntsync event create: %s\n", strerror(errno));
		return -1;
	}

	if (ioctl(fd, NTSYNC_IOC_EVENT_SET, &prev) < 0 || prev != 0) {
		printf("FAIL ntsync event set: prev=%u\n", prev);
		return -1;
	}
	prev = 0xdead;
	if (ioctl(fd, NTSYNC_IOC_EVENT_READ, &a) < 0 || !a.signaled) {
		printf("FAIL ntsync event read after set: manual=%u signaled=%u\n",
		       a.manual, a.signaled);
		return -1;
	}
	if (ioctl(fd, NTSYNC_IOC_EVENT_RESET, &prev) < 0 || prev != 1) {
		printf("FAIL ntsync event reset: prev=%u\n", prev);
		return -1;
	}

	printf("PASS ntsync event\n");
	return fd;
}

int main(void)
{
	int dev, sem;

	mkdir("/proc", 0555);
	mkdir("/sys", 0555);
	mkdir("/dev", 0755);
	mount("proc", "/proc", "proc", 0, NULL);
	mount("sysfs", "/sys", "sysfs", 0, NULL);
	mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);

	load_modules();

	dev = open("/dev/ntsync", O_RDWR | O_CLOEXEC);
	if (dev < 0) {
		printf("FAIL ntsync unavailable: %s\n", strerror(errno));
		goto out;
	}

	sem = sem_test(dev);
	if (sem >= 0)
		wait_test(dev, sem);
	mutex_test(dev);
	event_test(dev);
	close(dev);

out:
	printf("ntsync_probe: done (t=%llu ns)\n", now_ns());
	fflush(stdout);
	sync();
	reboot(RB_POWER_OFF);
	for (;;)
		pause();
	return 0;
}
