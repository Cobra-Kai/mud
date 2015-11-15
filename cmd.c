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
#include "cmd.h"
#include "grow.h"

typedef unsigned long hash_t;

static hash_t hash(const char *key)
{
	/* jenkins one-at-a-time hash */
	hash_t h = 0;
	while (*key) {
		h += *key++;
		h += h << 10;
		h += h >> 6;
	}
	h += h << 3;
	h ^= h >> 11;
	h += h << 15;
	return h;
}

/* find the first unused slot */
static void *hash_slot(const char *key, void *base, unsigned max, size_t elem, const char *(*getkey)(const void *obj))
{
	if (!max)
		return NULL;
	hash_t h = hash(key);
	unsigned mask = max ? max - 1 : 0;
	unsigned tries = max;
	while (tries-- > 0) {
		void *test = (char*)base + (h & mask) * elem;
		const char *check = getkey(test);
		if (!check)
			return test; /* unused slot */
		h += 65537; /* quick and dirty rehash */
	}
	return NULL; /* unable to find unused slot */
}

static void *hash_find(const char *key, void *base, unsigned max, size_t elem, const char *(*getkey)(const void *obj))
{
	if (!max)
		return NULL;
	hash_t h = hash(key);
	unsigned mask = max ? max - 1 : 0;
	unsigned tries = max;
	while (tries > 0) {
		void *test = (char*)base + (h & mask) * elem;
		const char *check = getkey(test);
		if (!check)
			return NULL; /* dead end */
		if (!strcmp(key, check))
			return test; /* match */
		h += 65537; /* quick and dirty rehash */
	}
	return NULL; /* not found */
}

struct command {
	char *name;
	void (*f)(void *p);
};

static struct command *command;
static unsigned command_max; /* must be a power of 2 */

static const char *command_getkey(const void *obj)
{
	const struct command *cmd = obj;
	return cmd->name;
}

int command_register(const char *name, void (*f)(void *p))
{
	/* look for a duplicate entry */
	struct command *cmd = hash_find(name, command, command_max, sizeof(*command), command_getkey);

	if (!cmd)  {
		/* not found, create it */
		cmd = hash_slot(name, command, command_max, sizeof(*command), command_getkey);
		if (!cmd) {
			/* resize if there isn't space */
			unsigned next = command_max ? command_max * 2 : 1;
			if (grow(&command, &command_max, next, sizeof(*command)))
				return -1;
			cmd = hash_slot(name, command, command_max, sizeof(*command), command_getkey);
		}
		if (!cmd)
			return -1;
	} else {
		/* free the old details */
		free(cmd->name);
		cmd->name = NULL;
		cmd->f = NULL;
	}
	/* initialize the entry */
	cmd->name = strdup(name);
	cmd->f = f;
	return 0;
}

int command_run(const char *name, void *p)
{
	struct command *cmd = hash_find(name, command, command_max, sizeof(*command), command_getkey);

	if (!cmd)
		return -1; /* error - not found */
	cmd->f(p);
	return 0;
}
