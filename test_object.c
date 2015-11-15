/*
 * Copyright 2015 Jon Mayo <jon@cobra-kai.com>
 *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdio.h>
#include <stdlib.h>
#include "object.h"
#include "rc.h"

int main()
{
	/* test code */
	struct object *a = obj_new();
	char *data[] = {
	"happy", "joy",
	"a", "100", "b", "200", "c", "300",
	"g", "700", "h", "800", "i", "900",
	"test", "1", "flag", "",
	"j", "1000", "k", "1100", "l", "1200",
	"m", "1300", "n", "1400",
	"d", "400", "e", "500", "f", "600",
	NULL, };
	int i;

	fprintf(stderr, "TEST1: %p\n", obj_get(a, "happy"));
	for (i = 0; data[i]; i += 2) {
		obj_set(a, data[i], data[i + 1]);
	}
	fprintf(stderr, "TEST2: %s\n", obj_get(a, "happy"));
	fprintf(stderr, "TEST3: %s\n", obj_get(a, "flag"));
	fprintf(stderr, "TEST4: %s\n", obj_get(a, "test"));

	const char test_path[] = "obj1.dat";
	{
		/* saving test */
		FILE *f = fopen(test_path, "w");
		if (!f) {
			perror(test_path);
			return EXIT_FAILURE;
		}
		int e = obj_save(a, f);
		if (e == -1) {
			fprintf(stderr, "%s():error!\n", "obj_save");
			return EXIT_FAILURE;
		}
		fclose(f);
		obj_release(a);
	}

	{
		/* loading test */
		FILE *f = fopen(test_path, "r");
		if (!f) {
			perror(test_path);
			return EXIT_FAILURE;
		}
		struct object *b = obj_load(f, test_path);
		if (!b) {
			fprintf(stderr, "%s():%s:error!\n", "obj_load", test_path);
			return EXIT_FAILURE;
		}

		fclose(f);

		/* dump the object */
		obj_save(b, stdout);

		obj_release(b);
	}

	return 0;
}
