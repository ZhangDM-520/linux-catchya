// SPDX-License-Identifier: GPL-2.0
/*
 * gfxoff_probe - PID 1 for a QEMU boot that loads amdgpu.ko and exercises the
 * gfx_off_delay_ms module parameter.
 *
 * QEMU has no RDNA 3.5 and no SMU, so this cannot observe GFXOFF itself. What
 * it can prove is everything the parameter is allowed to affect:
 *
 *   1. the module links, loads and registers its parameter with the expected
 *      default;
 *   2. the setter accepts the documented range (0 and 1000 ms) and rejects
 *      everything outside it, instead of silently clamping - a rejected write
 *      leaves the previous value in place, which is what param_set_uint_minmax
 *      guarantees;
 *   3. the write goes through the generic parameter layer, so the read-back
 *      is the value the driver will use.
 *
 * The SMU call path is untouched by the knob and cannot be reached here; that
 * part stays a hardware gate.
 *
 * Build: cc -O2 -static -o init gfxoff_probe.c
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <unistd.h>

#include "modload.h"

#define PARAM_PATH "/sys/module/amdgpu/parameters/gfx_off_delay_ms"

static int read_param(char *buf, size_t len)
{
	int fd = open(PARAM_PATH, O_RDONLY | O_CLOEXEC);
	ssize_t n;

	if (fd < 0) {
		printf("FAIL gfxoff param open: %s\n", strerror(errno));
		return -1;
	}
	n = read(fd, buf, len - 1);
	close(fd);
	if (n <= 0) {
		printf("FAIL gfxoff param read: %s\n", strerror(errno));
		return -1;
	}
	buf[n] = '\0';
	buf[strcspn(buf, "\n")] = '\0';
	return 0;
}

static int write_param(const char *val)
{
	int fd = open(PARAM_PATH, O_WRONLY | O_CLOEXEC);
	ssize_t n;

	if (fd < 0) {
		printf("FAIL gfxoff param open for write: %s\n", strerror(errno));
		return -1;
	}
	n = write(fd, val, strlen(val));
	close(fd);
	/* The caller decides whether a rejection is the expected outcome. */
	return n == (ssize_t)strlen(val) ? 0 : -1;
}

/* A rejected write must return an error and leave the previous value alone. */
static int expect_reject(const char *val)
{
	char buf[32];

	errno = 0;
	if (write_param(val) == 0) {
		printf("FAIL gfxoff param accepted '%s'\n", val);
		return -1;
	}
	if (read_param(buf, sizeof(buf)))
		return -1;
	if (strcmp(buf, "1000")) {
		printf("FAIL gfxoff param changed to '%s' by a rejected write\n",
		       buf);
		return -1;
	}
	printf("PASS gfxoff reject %s\n", val);
	return 0;
}

int main(void)
{
	char buf[32];

	mkdir("/proc", 0555);
	mkdir("/sys", 0555);
	mkdir("/dev", 0755);
	mount("proc", "/proc", "proc", 0, NULL);
	mount("sysfs", "/sys", "sysfs", 0, NULL);
	mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);

	load_modules();

	if (read_param(buf, sizeof(buf)))
		goto out;
	if (strcmp(buf, "100")) {
		printf("FAIL gfxoff default '%s', expected 100\n", buf);
		goto out;
	}
	printf("PASS gfxoff default 100\n");

	if (write_param("0") || read_param(buf, sizeof(buf)) || strcmp(buf, "0")) {
		printf("FAIL gfxoff set 0\n");
		goto out;
	}
	printf("PASS gfxoff set 0\n");

	if (write_param("1000") || read_param(buf, sizeof(buf)) ||
	    strcmp(buf, "1000")) {
		printf("FAIL gfxoff set 1000\n");
		goto out;
	}
	printf("PASS gfxoff set 1000\n");

	expect_reject("1001");
	expect_reject("-1");
	expect_reject("abc");

out:
	printf("gfxoff_probe: done\n");
	fflush(stdout);
	sync();
	reboot(RB_POWER_OFF);
	for (;;)
		pause();
	return 0;
}
