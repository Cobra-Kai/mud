/*
 * Copyright 2015 Jon Mayo <jon@cobra-kai.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "objdb.h"
#include "object.h"

struct objdb_txn {
	char *filename;
	char *tempfile;
	FILE *f;
};

static char *objdb_root = NULL; /* this is no default path */
static int objdb_fd = -1; /* use this directory for all openat() calls */

/* objdb_root_check() opens the root path.
 * return -1 on error, 0 on success */
static int objdb_root_check(void)
{
	if (objdb_fd == -1) {
		if (!objdb_root) {
			fprintf(stderr, "%s():please configure DB path\n", __func__);
			return -1;
		}
		objdb_fd = open(objdb_root, O_DIRECTORY);
	}

	if (objdb_fd == -1) {
		perror(objdb_root);
		return -1;
	}

	return 0;
}

/* objdb_temp() create a stream to store an object.
 * later the path stored in tempname will be renamed by objdb_commit() to the
 * target location. tempname is assumed to be PATH_MAX in size. */
static FILE *objdb_temp(char *tempname)
{
	if (objdb_root_check())
		return NULL;

	/* this routine is hardcoded to have a 6 digit pattern XXXXXX */
	int e = snprintf(tempname, PATH_MAX, "/tmp/obj.XXXXXX");
	if (e < 0 || e >= PATH_MAX) {
		perror(tempname);
		errno = EINVAL;
		return NULL;
	}
	int tmpofs = strlen(tempname) - 6;
	if (tmpofs <= 0) {
		fprintf(stderr, "%s():illegal temp pattern\n", __func__);
		errno = EINVAL;
		return NULL;
	}

	int fd;
	unsigned long seq = time(0);
	int tries = 1000; /* */
	do {
		/* generate a sequence number using a simple Lehmer RNG */
		seq = (16807UL * seq) % 2147483647UL;
		snprintf(tempname + tmpofs, 6, "%06u", rand() % 1000000);
		/* these temp files are always exclusive */
		int oflags = O_CREAT | O_EXCL | O_RDWR;
		errno = 0;
		fd = openat(objdb_fd, tempname, oflags, 0666);
		if (fd < 0 && errno != EEXIST) {
			fprintf(stderr, "%s():%s:temp file:%s\n", __func__,
				tempname, strerror(errno));
			return NULL;
		}
	} while (fd < 0 && tries--);
	if (fd < 0) {
		fprintf(stderr, "%s():unable to open a temp file\n", __func__);
		return NULL;
	}

	return fdopen(fd, "r+");
}

/* objdb_txn_destroy() frees all allocations related to a transaction. */
static void objdb_txn_destroy(struct objdb_txn *txn)
{
	fclose(txn->f);
	free(txn->tempfile);
	txn->tempfile = NULL;
	free(txn->filename);
	txn->filename = NULL;
}

/* objdb_start() creates a transaction for a target at path.
 * these transactions are destructive if committed and assume an obj_save() will be used.
 */
struct objdb_txn *objdb_start(const char *path)
{
	struct objdb_txn *txn = calloc(1, sizeof(*txn));
	if (!txn) {
		perror(__func__);
		return NULL;
	}

	txn->tempfile = malloc(PATH_MAX);
	txn->filename = strdup(path);
	txn->f = objdb_temp(txn->tempfile);
	return txn;
}

struct object *objdb_load(const char *path)
{
	errno = 0;
	int fd = openat(objdb_fd, path, O_RDONLY);
	if (fd < 0) {
		perror(path);
		return NULL;
	}
	FILE *f = fdopen(fd, "r");
	if (!f) {
		perror(path);
		close(fd);
		return NULL;
	}
	struct object *obj = obj_load(f, path); /* use the filename as the tag for error messages */
	fclose(f);
	return obj; /* obj could be NULL if obj_load() failed */
}

/* objdb_f() return the FILE* handle for the current object. */
FILE *objdb_f(struct objdb_txn *txn)
{
	return txn->f;
}

int objdb_commit(struct objdb_txn *txn)
{
	if (objdb_root_check())
		return NULL;

	int e = renameat(objdb_fd, txn->tempfile, objdb_fd, txn->filename);
	if (e) {
		perror(txn->filename);
	}
	objdb_txn_destroy(txn);
	return e == 0;
}

int objdb_rollback(struct objdb_txn *txn)
{
	if (objdb_root_check())
		return NULL;

	int e = unlinkat(objdb_fd, txn->tempfile, 0);
	if (e) {
		perror(txn->tempfile);
	}
	objdb_txn_destroy(txn);
	return e == 0;
}

/* objdb_setroot configures the DB path.
 * returns 0 on success, and -1 on failure. */
int objdb_setroot(const char *path)
{
	if (objdb_fd != -1 || !path) {
		/* we don't permit this because some txn might be outstanding
		 * and weird things could potentially happen, like the /tmp
		 * file and the target file could end up in different
		 * directories. -jon */
		fprintf(stderr, "%s():changing DB path not permitted after initialization\n", __func__);
		return -1;
	}
	free(objdb_root);
	objdb_root = strdup(path);
	return objdb_root_check();
}
