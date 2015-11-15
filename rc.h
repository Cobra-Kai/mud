#ifndef RC_H
#define RC_H
#define RETAIN(m) do { (m)->rc++; } while (0)
#define RELEASE(m, do_free) do { \
	if (!--(m)->rc) { (do_free)(m); (m) = NULL; } \
	} while (0)
#endif
