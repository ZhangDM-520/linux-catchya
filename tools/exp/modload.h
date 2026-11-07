/* SPDX-License-Identifier: GPL-2.0 */
/*
 * modload.h - load every module that vmtest.sh packed into /modules.
 *
 * The numeric filename prefix vmtest.sh writes ("01-ntsync.ko") is not a
 * dependency order, only a stable one: the caller lists dependencies first in
 * WITH_MODULES. A failed load prints "FAIL module ..." on stdout so the boot
 * log is the contract - a probe's exit status is not observable from QEMU.
 *
 * Included by the probes as a static helper; it has no other user.
 */
#ifndef MODLOAD_H
#define MODLOAD_H

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static int modload_cmp(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void load_modules(void)
{
	const char *dir = "/modules";
	char *names[32];
	struct dirent *de;
	DIR *d;
	int n = 0;

	d = opendir(dir);
	if (!d) {
		printf("module dir absent\n");
		return;
	}
	while ((de = readdir(d)) && n < 32) {
		size_t len = strlen(de->d_name);

		if (len > 3 && !strcmp(de->d_name + len - 3, ".ko"))
			names[n++] = strdup(de->d_name);
	}
	closedir(d);

	qsort(names, n, sizeof(names[0]), modload_cmp);
	for (int i = 0; i < n; i++) {
		char path[256];
		int fd;

		snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
		fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd < 0) {
			printf("FAIL module %s open: %s\n", names[i],
			       strerror(errno));
		} else {
			if (syscall(SYS_finit_module, fd, "", 0) < 0)
				printf("FAIL module %s load: %s\n", names[i],
				       strerror(errno));
			else
				printf("PASS module %s\n", names[i]);
			close(fd);
		}
		free(names[i]);
	}
}

#endif /* MODLOAD_H */
