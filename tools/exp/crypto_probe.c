// SPDX-License-Identifier: GPL-2.0
/*
 * crypto_probe - PID 1 for a QEMU boot that checks which crypto drivers win.
 *
 * "the config has CRYPTO_AES_NI_INTEL=m" is not evidence that AES runs on
 * AES-NI, so this probe loads the packed modules and reads /proc/crypto.
 *
 * Only gcm(aes) can be answered from /proc/crypto: aesni-intel registers one
 * algorithm per SIMD tier and the crypto API picks the highest priority, so
 * the driver name of the winner names the tier. aes, sha256 and chacha20 have
 * a single registration here - the lib wrappers, which select the arch
 * implementation through a static key inside their own call path. Their
 * driver name is therefore always the same; the probe prints the winner and
 * the CPU features that gate that key, and claims nothing more.
 *
 * Nothing here is a claim about throughput.
 *
 * Build: cc -O2 -static -o init crypto_probe.c
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

#include "modload.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define CRYPTO_PATH "/proc/crypto"
#define CPUINFO_PATH "/proc/cpuinfo"

static char cpu_flags[4096];

/* Keep the first "flags" line of /proc/cpuinfo, padded for strstr(). */
static void read_cpu_flags(void)
{
	FILE *f = fopen(CPUINFO_PATH, "r");
	char line[4096];

	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char *val;
		size_t len;

		if (strncmp(line, "flags", 5))
			continue;
		val = strchr(line, ':');
		if (!val)
			break;
		val++;
		len = strcspn(val, "\n");
		snprintf(cpu_flags, sizeof(cpu_flags), " %.*s ", (int)len, val);
		break;
	}
	fclose(f);
}

static int cpu_has(const char *flag)
{
	char pat[64];

	if (snprintf(pat, sizeof(pat), " %s ", flag) >= (int)sizeof(pat))
		return 0;
	return strstr(cpu_flags, pat) != NULL;
}

/*
 * aesni-intel registers one gcm(aes) per tier, gated on the features the tier
 * uses (arch/x86/crypto/aesni-intel_glue.c: register_avx_algs() and
 * aesni_init()).  The winner's driver name therefore names the tier the boot
 * landed on.  PREFER_YMM demotes the AVX-512 entry to priority 1 on parts
 * where the kernel prefers 256-bit vectors, so a vaes-avx2 win with AVX-512
 * present is correct, not a failure: the check is "the winner is a tier this
 * CPU can run", and the fallback to generic C is what fails.
 */
static const char *const gcm_tiers[] = {
	"vaes-avx512", "vaes-avx2", "aesni-avx", "aesni",
};

static int tier_usable(const char *tier)
{
	if (!strcmp(tier, "vaes-avx512"))
		return cpu_has("avx2") && cpu_has("vaes") &&
		       cpu_has("avx512bw") && cpu_has("avx512vl") &&
		       cpu_has("bmi2") && cpu_has("pclmulqdq");
	if (!strcmp(tier, "vaes-avx2"))
		return cpu_has("avx2") && cpu_has("vaes") &&
		       cpu_has("vpclmulqdq") && cpu_has("pclmulqdq");
	if (!strcmp(tier, "aesni-avx"))
		return cpu_has("avx") && cpu_has("pclmulqdq");
	return cpu_has("aes");
}

struct best {
	char name[64];
	char driver[64];
	long priority;
	int found;
};

static void consider(struct best *b, const char *name, const char *driver,
		     long prio)
{
	if (!b->found || prio > b->priority) {
		snprintf(b->name, sizeof(b->name), "%s", name);
		snprintf(b->driver, sizeof(b->driver), "%s", driver);
		b->priority = prio;
		b->found = 1;
	}
}

int main(void)
{
	struct best aes = { 0 }, gcm = { 0 }, sha256 = { 0 }, chacha = { 0 };
	char line[512], name[64] = "", driver[64] = "";
	long prio = 0;
	FILE *f;
	int bad = 0;

	mkdir("/proc", 0555);
	mkdir("/sys", 0555);
	mkdir("/dev", 0755);
	if (mount("proc", "/proc", "proc", 0, NULL) ||
	    mount("sysfs", "/sys", "sysfs", 0, NULL) ||
	    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL)) {
		printf("FAIL mount: cannot mount pseudo-filesystems\n");
		goto out;
	}

	load_modules();
	read_cpu_flags();

	f = fopen(CRYPTO_PATH, "r");
	if (!f) {
		printf("FAIL crypto: %s missing\n", CRYPTO_PATH);
		goto out;
	}

	/* /proc/crypto is blank-line separated "key : value" blocks. */
	while (fgets(line, sizeof(line), f)) {
		char *sep = strchr(line, ':');

		if (!sep) {
			/* end of a block: account for it, then reset */
			if (name[0]) {
				if (!strcmp(name, "aes"))
					consider(&aes, name, driver, prio);
				else if (!strcmp(name, "gcm(aes)"))
					consider(&gcm, name, driver, prio);
				else if (!strcmp(name, "sha256"))
					consider(&sha256, name, driver, prio);
				else if (!strcmp(name, "chacha20"))
					consider(&chacha, name, driver, prio);
			}
			name[0] = driver[0] = '\0';
			prio = 0;
			continue;
		}

		*sep = '\0';
		{
			char *key = line;
			char *val = sep + 1;
			char *e;

			while (isspace((unsigned char)*key))
				key++;
			/* /proc/crypto pads the key to a fixed column */
			e = key + strlen(key);
			while (e > key && isspace((unsigned char)e[-1]))
				*--e = '\0';
			while (*val == ' ' || *val == '\t')
				val++;
			val[strcspn(val, "\n")] = '\0';

			if (!strcmp(key, "name"))
				snprintf(name, sizeof(name), "%s", val);
			else if (!strcmp(key, "driver"))
				snprintf(driver, sizeof(driver), "%s", val);
			else if (!strcmp(key, "priority"))
				prio = strtol(val, NULL, 10);
		}
	}
	fclose(f);

#define INFO(b, label, note)						\
	do {									\
		if (!(b).found)						\
			printf("SKIP crypto %-8s not registered\n", label);	\
		else								\
			printf("INFO crypto %-8s %s prio=%ld (%s)\n",		\
			       label, (b).driver, (b).priority, note);	\
	} while (0)

	printf("cpuflags aes=%d sha_ni=%d vaes=%d avx2=%d avx512f=%d pclmulqdq=%d\n",
	       cpu_has("aes"), cpu_has("sha_ni"), cpu_has("vaes"),
	       cpu_has("avx2"), cpu_has("avx512f"), cpu_has("pclmulqdq"));

	if (!gcm.found) {
		printf("FAIL crypto gcm(aes) not registered\n");
		bad++;
	} else {
		const char *best = NULL, *hit = NULL;

		for (size_t i = 0; i < ARRAY_SIZE(gcm_tiers); i++) {
			if (!tier_usable(gcm_tiers[i]))
				continue;
			if (!best)
				best = gcm_tiers[i];
			if (strstr(gcm.driver, gcm_tiers[i])) {
				hit = gcm_tiers[i];
				break;
			}
		}
		if (hit) {
			printf("PASS crypto gcm(aes) %s prio=%ld (tier %s, best available %s)\n",
			       gcm.driver, gcm.priority, hit,
			       best ? best : "none");
		} else {
			printf("FAIL crypto gcm(aes) %s prio=%ld (no SIMD tier, CPU supports %s)\n",
			       gcm.driver, gcm.priority, best ? best : "no tier");
			bad++;
		}
	}

	INFO(aes, "aes", "lib wrapper, tier is a static-key branch");
	INFO(sha256, "sha256", "lib wrapper, tier is a static-key branch");
	INFO(chacha, "chacha20", "lib wrapper, tier is a static-key branch");

out:
	printf("crypto_probe: done\n");
	fflush(stdout);
	sleep(1);
	sync();
	reboot(RB_POWER_OFF);
	for (;;)
		pause();
	return bad;
}
