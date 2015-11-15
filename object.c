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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "object.h"
#include "rc.h"
#include "cencode.h"

struct object
{
	unsigned prop_len, prop_max;
	char **prop; /* entries are stored as "key\0value\0" */
	int rc;
};

struct object *obj_new(void)
{
	struct object *o = calloc(1, sizeof(*o));
	RETAIN(o);
	return o;
}

void obj_retain(struct object *o)
{
	RETAIN(o);
}

void obj_release(struct object *o)
{
	RELEASE(o, obj_free);
}

void obj_free(struct object *o)
{
	if (!o)
		return;
	/* print an warning about destruction of referenced object */
	if (o->rc) {
		fprintf(stderr,
			"WARNING:%s():object %p still have references (rc=%d)\n",
			__func__, o, o->rc);
	}
	while (o->prop_len) {
		unsigned i = --o->prop_len;
		free(o->prop[i]);
		o->prop[i] = NULL;
	}
	free(o->prop);
	free(o);
}

static int obj_compar(const void *a, const void *b)
{
	return strcmp(*(char**)a, *(char**)b);
}

/* return offset or -1 on error. */
static int obj_lookup_offset(struct object *o, const char *name)
{
	if (!o->prop_len)
		return -1; /* no match because list is empty */
	char **res = bsearch(&name, o->prop, o->prop_len, sizeof(*o->prop), obj_compar);

	if (res)
		return res - o->prop;

	return -1; /* no match */
}

const char *obj_get(struct object *o, const char *name)
{
	int ofs = obj_lookup_offset(o, name);
	if (ofs < 0)
		return NULL;
	char *s = o->prop[ofs];
	return s + strlen(s) + 1;
}

/* create a new entry. */
static char *obj_alloc_buffer(const char *name, const char *value)
{
	int namelen = strlen(name) + 1;
	int valuelen = strlen(value) + 1;

	char *s = malloc(namelen + valuelen);
	if (!s) {
		perror(__func__);
		return NULL;
	}
	memcpy(s, name, namelen);
	memcpy(s + namelen, value, valuelen);
	return s;
}

int obj_set(struct object *o, const char *name, const char *value)
{
	int ofs = obj_lookup_offset(o, name);
	if (ofs >= 0) {
		char *s = obj_alloc_buffer(name, value);
		if (!s) {
			return -1;
		}

		free(o->prop[ofs]); /* free the old entry */
		o->prop[ofs] = s; /* use the new entry */

		return 0; /* successfully updated */
	}

	/* make space if the length would exceed the allocated space */
	if (o->prop_len >= o->prop_max) {
		unsigned newsize = (o->prop_len + 1) * sizeof(*o->prop);

		/* round up to next power of 2 */
		newsize--;
		newsize |= newsize >> 1;
		newsize |= newsize >> 2;
		newsize |= newsize >> 4;
		newsize |= newsize >> 8;
		newsize |= newsize >> 16;
		newsize++;

		char **newprop = realloc(o->prop, newsize);
		if (!newprop) {
			perror(__func__);
			return -1;
		}
		o->prop = newprop;
		o->prop_max = newsize / sizeof(*o->prop);
	}

	char *s = obj_alloc_buffer(name, value);
	if (!s) {
		return -1;
	}

	/* set new entry and increment length */
	o->prop[o->prop_len++] = s;

	/* the bsearch() requires the array to be sorted */
	qsort(o->prop, o->prop_len, sizeof(*o->prop), obj_compar);

	return 0;
}

/* creates an iterator, but it's important that object is not modified until completed. */
struct object_iter obj_iter_new(struct object *o)
{
	struct object_iter it = { .o = o };
	return it;
}

/* return 0 on end of list, and 1 if there are more items */
int obj_iter_next(struct object_iter *it, const char **name, const char **value)
{
	struct object *o = it->o;
	if (it->i >= o->prop_len) {
		return 0;
	}
	const char *s = o->prop[it->i++];

	if (name)
		*name = s;
	if (value)
		*value = s + strlen(s) + 1;
	return 1;
}

/* limit ourselves to a 1MB buffer */
static __thread char obj_line_buf[1 << 20];
static __thread int obj_line_buf_max = sizeof(obj_line_buf);

/* save to an open file */
int obj_save(struct object *o, FILE *f)
{
	struct object_iter it = obj_iter_new(o);
	const char *name, *value;
	while (obj_iter_next(&it, &name, &value)) {
		int outlen;
		char *cur = obj_line_buf;
		int rem = obj_line_buf_max - 1;
		/* name */
		size_t namelen = strlen(name);
		/* TODO: remove '=' from the key as it will mess up obj_load() */
		outlen = c_encode(cur, rem, name, namelen);
		if (outlen == -1 || outlen > rem - 1)
			return -1; /* failure */
		cur += outlen;
		rem -= outlen;
		/* seperator */
		*cur++ = '=';
		rem--;
		/* value */
		size_t valuelen = strlen(value);
		outlen = c_encode(cur, rem, value, valuelen);
		if (outlen == -1 || outlen > rem - 1)
			return -1; /* failure */
		cur += outlen;
		rem -= outlen;
		/* terminator */
		*cur++ = '\n';
		*cur = 0;
		rem--;
		/* output */
		fputs(obj_line_buf, f);
	}
	fputs("%%END%%\n", f);
	return 0;
}

/* create a new object and load from an open file.
 * optionally a tag can be provided for error messages. */
struct object *obj_load(FILE *f, const char *tag)
{
	struct object *o = obj_new();
	int end_of_file = 0; /* look for "%%END%%" */
	int line = 0;
	if (!tag)
		tag = __func__;

	while (fgets(obj_line_buf, obj_line_buf_max, f)) {
		line++;
		char *end = strrchr(obj_line_buf, '\n');
		if (!end) {
			fprintf(stderr,
				"ERROR:%s:%d:truncated file or line exceeds maximum length!\n",
				tag, line);
			RELEASE(o, obj_free);
			return NULL;
		}
		*end = 0; /* discard the newline */
		if (!strcmp("%%END%%", obj_line_buf)) {
			end_of_file = 1;
			break;
		}
		/* process the line as name=value */
		char *value = strchr(obj_line_buf, '=');
		if (!value) {
			fprintf(stderr,
				"ERROR:%s:%d:line missing separator!\n",
				tag, line);
			RELEASE(o, obj_free);
			return NULL;
		}
		*value = 0;
		value++;

		int e;
		/* obj_line_buf is name, decode name in place */
		char *name = obj_line_buf;
		size_t namemax = strlen(obj_line_buf) + 1;
		e = c_decode(obj_line_buf, namemax, obj_line_buf, namemax);
		if (e == -1)
			goto parse_error;
		/* sep is value, decode value in place */
		size_t valuemax = strlen(value) + 1;
		e = c_decode(value, valuemax, value, valuemax);
		if (e == -1)
			goto parse_error;

		e = obj_set(o, name, value);
		if (e == -1) {
			fprintf(stderr,
				"ERROR:%s:%d:unable to set property!\n",
				tag, line);
			RELEASE(o, obj_free);
			return NULL;
		}
	}

	if (!end_of_file) {
		fprintf(stderr,
			"ERROR:%s:%d:truncated file missing END tag!\n",
			tag, line);
		RELEASE(o, obj_free);
		return NULL;
	}

	return o;
parse_error:
	fprintf(stderr,
		"ERROR:%s:%d:parse error!\n",
		tag, line);
	RELEASE(o, obj_free);
	return NULL;
}
