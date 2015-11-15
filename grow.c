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
#include <string.h>
#include "grow.h"

int grow(void *ptr, unsigned *max, unsigned min, size_t elem)
{
	unsigned old = *max * elem;
	unsigned len = min * elem;

	if (len <= old)
		return 0; /* nothing to do */

	len--;
	len |= len >> 1;
	len |= len >> 2;
	len |= len >> 4;
	len |= len >> 8;
	len |= len >> 16;
	len++;
	void *newptr = realloc(*(char**)ptr, len);
	if (!newptr) {
		perror(__func__);
		return -1;
	}
	memset(newptr + old, 0, len - old);
	*max = len / elem;
	*(char**)ptr = newptr;
	return 0;
}
