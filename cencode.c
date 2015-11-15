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
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>

#include "cencode.h"

/* c_encode places null terminated string into dst.
 * return written length on success. -1 on failure. */
int c_encode(char *dst, size_t dstmax, const char *src, size_t srclen)
{
	size_t dstlen = 0;
	while (srclen > 0 && *src) {
		/* check that there is room for at least one more '\nnn' and a null */
		if (dstlen + 4 + 1 >= dstmax)
			return -1; /* overflow */
		char c = *src++;
		srclen--;
		/* check for typical C sequences */
		switch (c) {
		case '\\':
			dst[dstlen++] = '\\';
			dst[dstlen++] = '\\';
			break;
		case '\a':
			dst[dstlen++] = '\\';
			dst[dstlen++] = 'a';
			break;
		case '\b':
			dst[dstlen++] = '\\';
			dst[dstlen++] = 'b';
			break;
		case '\f':
			dst[dstlen++] = '\\';
			dst[dstlen++] = 'f';
			break;
		case '\n':
			dst[dstlen++] = '\\';
			dst[dstlen++] = 'n';
			break;
		case '\r':
			dst[dstlen++] = '\\';
			dst[dstlen++] = 'r';
			break;
		case '\t':
			dst[dstlen++] = '\\';
			dst[dstlen++] = 't';
			break;
		case '\v':
			dst[dstlen++] = '\\';
			dst[dstlen++] = 'v';
			break;
		default:
			if (isprint(c)) {
				dst[dstlen++] = c;
			} else {
				dst[dstlen++] = '\\';
				dst[dstlen++] = '0' + (c % 8);
				dst[dstlen++] = '0' + ((c / 8) % 8);
				dst[dstlen++] = '0' + ((c / 64) % 8);
			}
		}
	}
	dst[dstlen] = 0;
	// fprintf(stderr, "RES=\"%.*s\"\n", dstlen, dst);
	return dstlen;
}

/* c_decode parse a string of escape sequences into dst.
 * dst will be null terminated.
 * return written length on success. -1 on failure. */
int c_decode(char *dst, size_t dstmax, const char *src, size_t srclen)
{
	size_t dstlen = 0;
	unsigned i;
	long v;
	char hexbuf[64];
	while (srclen > 0 && *src) {
		if (dstlen + 1 >= dstmax)
			return -1; /* overflow */
		char c = *src++;
		srclen--;
		if (c == '\\') {
			if (!srclen)
				return -1; /* unterminated escape */
			/* make decision on next character */
			c = *src++;
			srclen--;
			switch (c) {
			/* octal */
			case '0': case '1': case '2': case '3':
			case '4': case '5': case '6': case '7':
				/* parse octal number - up to 3 digits */
				v = 0;
				for (i = 0; (c >= '0' && c <= '7') && i < 3; i++) {
					v = (v * 8) + (c - '0');
					/* stop at end of src */
					if (srclen) {
						c = *src++;
						srclen--;
					} else {
						break;
					}
				}
				dst[dstlen++] = v;
				break;
			/* hex */
			case 'x':
				/* parse hex number - any amount could be read.
				 * stop at end of src buffer or first non-hex */
				for (i = 0; srclen > 0 && i < sizeof(hexbuf) - 1; i++) {
					c = *src++;
					srclen--;
					if (!isxdigit(c))
						break;
					hexbuf[i] = c;
				}
				hexbuf[i] = 0;

				errno = 0;
				v = strtol(src, NULL, 16);
				if (errno)
					return -1; /* parse error */
				dst[dstlen++] = v;
				break;
			case '\\':
				dst[dstlen++] = '\\';
				break;
			case 'a':
				dst[dstlen++] = '\a';
				break;
			case 'b':
				dst[dstlen++] = '\b';
				break;
			case 'f':
				dst[dstlen++] = '\f';
				break;
			case 'n':
				dst[dstlen++] = '\n';
				break;
			case 'r':
				dst[dstlen++] = '\r';
				break;
			case 't':
				dst[dstlen++] = '\t';
				break;
			case 'v':
				dst[dstlen++] = '\v';
				break;
			default:
				return -1; /* illegal escape */
			}
		} else {
			dst[dstlen++] = c;
		}
	}
	dst[dstlen] = 0;
	return dstlen;
}
