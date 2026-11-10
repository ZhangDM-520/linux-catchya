// SPDX-License-Identifier: GPL-2.0
/*
 * sched_defaults_probe - PID 1 for a QEMU boot that checks the scheduler
 * defaults this tree carries, not just that the kernel boots.
 *
 * Two CachyOS defaults are invisible to a compile-only check:
 *   - CACHY: Change default cgroup_mode to max  -> sched/cgroup_mode
 *   - CACHY: Disable DELAY_DEQUEUE              -> sched/features tokens
 * and BORE is only in the tree when CONFIG_SCHED_BORE built it, which shows up
 * as the sched base_slice_ns node. The probe reads the debugfs surface and
 * reports each one; a missing node is a FAIL, not a skip.
 *
 * Build: cc -O2 -static -o init sched_defaults_probe.c
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <unistd.h>

#define SCHED_DEBUGFS "/sys/kernel/debug/sched"
#define FEATURES_PATH SCHED_DEBUGFS "/features"
#define CGROUP_PATH SCHED_DEBUGFS "/cgroup_mode"
#define SLICE_PATH SCHED_DEBUGFS "/base_slice_ns"

static int read_file(const char *path, char *buf, size_t len)
{
	FILE *f = fopen(path, "r");
	size_t n;

	if (!f)
		return -1;
	n = fread(buf, 1, len - 1, f);
	fclose(f);
	buf[n] = '\0';
	while (n && isspace((unsigned char)buf[n - 1]))
		buf[--n] = '\0';
	return 0;
}

/* Whole-token match: "DELAY_DEQUEUE" must not match "NO_DELAY_DEQUEUE". */
static int has_token(const char *list, const char *tok)
{
	char pat[64];

	if (snprintf(pat, sizeof(pat), " %s ", tok) >= (int)sizeof(pat))
		return 0;
	return strstr(list, pat) != NULL;
}

int main(void)
{
	char features[8192], mode[64];
	int bad = 0;

	mkdir("/proc", 0555);
	mkdir("/sys", 0555);
	mkdir("/dev", 0755);
	mkdir("/sys/kernel/debug", 0755);
	if (mount("proc", "/proc", "proc", 0, NULL) ||
	    mount("sysfs", "/sys", "sysfs", 0, NULL) ||
	    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL) ||
	    mount("debugfs", "/sys/kernel/debug", "debugfs", 0, NULL)) {
		printf("FAIL mount: cannot mount pseudo-filesystems\n");
		goto out;
	}

	/* The file lists every mode and wraps the active one: "up concur (max)". */
	if (read_file(CGROUP_PATH, mode, sizeof(mode))) {
		printf("FAIL cgroup_mode: %s missing\n", CGROUP_PATH);
		bad++;
	} else if (!strstr(mode, "(max)")) {
		printf("FAIL cgroup_mode: %s (want (max))\n", mode);
		bad++;
	} else {
		printf("PASS cgroup_mode: %s\n", mode);
	}

	if (read_file(FEATURES_PATH, features, sizeof(features))) {
		printf("FAIL features: %s missing\n", FEATURES_PATH);
		bad++;
	} else {
		/* Pad so the first and last tokens match like the middle ones. */
		char padded[8192 + 3];
		int n = snprintf(padded, sizeof(padded), " %s ", features);

		if (n < 0 || n >= (int)sizeof(padded)) {
			printf("FAIL features: list too long\n");
			bad++;
		} else if (has_token(padded, "DELAY_DEQUEUE")) {
			printf("FAIL DELAY_DEQUEUE: on (want NO_DELAY_DEQUEUE)\n");
			bad++;
		} else if (!has_token(padded, "NO_DELAY_DEQUEUE")) {
			printf("FAIL DELAY_DEQUEUE: absent from features\n");
			bad++;
		} else {
			printf("PASS DELAY_DEQUEUE: off\n");
		}

		if (has_token(padded, "DELAY_ZERO")) {
			printf("FAIL DELAY_ZERO: on (want NO_DELAY_ZERO)\n");
			bad++;
		} else if (!has_token(padded, "NO_DELAY_ZERO")) {
			printf("FAIL DELAY_ZERO: absent from features\n");
			bad++;
		} else {
			printf("PASS DELAY_ZERO: off\n");
		}
	}

	if (access(SLICE_PATH, R_OK)) {
		printf("FAIL BORE: %s missing (CONFIG_SCHED_BORE off?)\n",
		       SLICE_PATH);
		bad++;
	} else {
		printf("PASS BORE: %s present\n", SLICE_PATH);
	}

out:
	printf("%s sched defaults\n", bad ? "FAIL" : "PASS");
	printf("sched_defaults_probe: done\n");
	fflush(stdout);
	sleep(1);
	sync();
	reboot(RB_POWER_OFF);
	for (;;)
		pause();
	return bad;
}
