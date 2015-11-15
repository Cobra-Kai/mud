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
/******************************************************************************/
#include <assert.h>
#include <errno.h>
#include <locale.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <libgen.h>
#include <search.h>
#include <unistd.h>

#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "cmd.h"
#include "objdb.h"
#include "object.h"
#include "rc.h"

/******************************************************************************/
#define container_of(ptr, type, member) \
	((type*)(char*)(ptr) - offsetof(type, member))

/******************************************************************************/

#define DLIST_INSERT_AFTER(head, item) do { \
	(item)->prev = (head); \
	(item)->next = *(head); \
	*(head) = (item); \
	} while (0)
#define DLIST_REMOVE(item) do { \
	if ((item)->prev) *(item)->prev = (item)->next; \
	(item)->prev = NULL; \
	(item)->next = NULL; \
	} while (0)

/******************************************************************************/

#define BUF_CHECK(m, len) ((len) <= (sizeof ((m)->buf) - (m)->bufofs))

/* assumed BUF_CHECK already passed */
#define BUF_APPEND(m, data, len) do { \
	assert(BUF_CHECK((m), (len)); \
	memcpy(&(m)->buf[(m)->ofs], (data), (len)); \
	(m)->ofs += (len); \
	} while (0)

/******************************************************************************/

struct prop {
	char *n;
	char *v;
};

/******************************************************************************/

/* flags for struct socket->event() */
#define EVENT_READ (1)
#define EVENT_WRITE (2)

typedef int SOCKET;
#define INVALID_SOCKET (-1)

struct sockbase {
	SOCKET fd;
	int rc;
	void (*event)(SOCKET fd, struct sockbase *ptr, long event);
	void (*free)(void *ptr);
};

#define SOCKMAX 256

static struct socket_info {
	SOCKET fd; /* this is redundant because of sockbase */
	struct sockbase *ptr;
} sockets[SOCKMAX];
static int sockets_max = sizeof(sockets) / sizeof(*sockets);
static int sockets_fdmax;
static int sockets_count;
static fd_set sockets_rfds, sockets_wfds;

void sockerror(const char *reason)
{
	fprintf(stderr, "%s:%s\n", reason, strerror(errno));
}

void sockclose(SOCKET fd)
{
	if (fd != INVALID_SOCKET) {
		close(fd);
		sockets_count--;
	} else {
		fprintf(stderr, "%s:fd is invalid!\n", __func__);
	}
}

int sockset(SOCKET fd, long events)
{
	if (fd == INVALID_SOCKET || fd >= sockets_max)
		return -1;
	if (events & EVENT_READ)
		FD_SET(fd, &sockets_rfds);
	if (events & EVENT_WRITE)
		FD_SET(fd, &sockets_wfds);
	return 0;
}

int sockclr(SOCKET fd, long events)
{
	if (fd == INVALID_SOCKET || fd >= sockets_max)
		return -1;
	if (events & EVENT_READ)
		FD_CLR(fd, &sockets_rfds);
	if (events & EVENT_WRITE)
		FD_CLR(fd, &sockets_wfds);
	return 0;
}

int sockadd(SOCKET fd, struct sockbase *ptr, long events,
	void (*event)(SOCKET fd, struct sockbase *ptr, long event),
	void (*free)(struct sockbase *ptr))
{
	if (fd == INVALID_SOCKET || fd >= sockets_max)
		return -1;
	if (fd > sockets_fdmax)
		sockets_fdmax = fd;
	sockets_count++;
	sockets[fd].fd = fd; /* this is somewhat redundant */
	sockets[fd].ptr = ptr;
	ptr->event = event;
	ptr->free = (void(*)(void*))free;
	sockset(fd, events);
	return 0;
}

int sockpoll(void)
{
	if (!sockets_count)
		return -1;

	fd_set rfds = sockets_rfds;
	fd_set wfds = sockets_wfds;

	struct timeval next = { .tv_sec = 300 }; // TODO: find next timer
	int n = select(sockets_fdmax + 1, &rfds, &wfds, NULL, &next);
	if (n < 0) {
		sockerror("select()");
		return -1;
	}

	int i;
	for (i = 0; i < sockets_max; i++) {
		if (sockets[i].ptr == NULL)
			continue;
		int fd = sockets[i].fd;
		if (fd == INVALID_SOCKET)
			continue;
		int event = 0;
		if (FD_ISSET(fd, &rfds))
			event |= EVENT_READ;
		if (FD_ISSET(fd, &wfds))
			event |= EVENT_WRITE;

		if (event) {
			struct sockbase *s = sockets[i].ptr;
			RETAIN(s);
			s->event(fd, sockets[i].ptr, event);
			RELEASE(s, s->free);
		}
	}

	return 0;
}

/******************************************************************************/
struct object *system_env; /* system environment options */

/******************************************************************************/
/* connection stream - can be used by servers or clients */
struct connection {
	struct sockbase sockbase;
	/* input buffer */
	unsigned buflen;
	unsigned bufmax;
	char buf[512];
	/* output buffer */
	unsigned outbuf_len, outbuf_max;
	char outbuf[16384];
};

void connection_init(struct connection *c, SOCKET fd)
{
	c->sockbase.fd = fd;
	c->buflen = 0;
	c->bufmax = sizeof(c->buf); // TODO: support dynamic allocation
	c->outbuf_len = 0;
	c->outbuf_max = sizeof(c->outbuf); // TODO: support dynamic allocation
}

int connection_vprintf(struct connection *c, const char *fmt, va_list ap)
{
	int rem = (int)c->outbuf_max - (int)c->outbuf_len;
	if (rem > 0) {
		int e = vsnprintf(c->outbuf + c->outbuf_len, rem, fmt, ap);
		if (e <= rem) {
			c->outbuf_len += e;
		} else {
			fprintf(stderr, "WARNING:%s():output truncated (rem=%d e=%d)\n", __func__, rem, e);
			c->outbuf_len = c->outbuf_max;
			e = -1; /* treat this as an error */
		}
		sockset(c->sockbase.fd, EVENT_WRITE);
		return e;
	} else {
		fprintf(stderr, "WARNING:%s():connection buffer full (max=%d len=%d)\n", __func__, c->outbuf_max, c->outbuf_len);
		return -1;
	}
}

int connection_printf(struct connection *c, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
int connection_printf(struct connection *c, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int e = connection_vprintf(c, fmt, ap);
	va_end(ap);
	return e;
}

/******************************************************************************/
/* each remote client is serviced by a single server instance */
struct server {
	struct connection c;
	struct server *next, **prev;
	struct object *env; /* current environment */
};

static void server_close(struct server *s)
{
	SOCKET fd = s->c.sockbase.fd;
	if (fd != INVALID_SOCKET) {
		FD_CLR(fd, &sockets_rfds);
		FD_CLR(fd, &sockets_wfds);
		sockclose(fd);
		s->c.sockbase.fd = INVALID_SOCKET;
	}
}

void server_free(struct server *s)
{
	server_close(s);
	obj_release(s->env);
	free(s);
}

static void server_free_sockbase(struct sockbase *base)
{
	struct connection *c = container_of(base, struct connection, sockbase);
	struct server *s = container_of(c, struct server, c);
	server_free(s);
}

static void server_event(SOCKET fd, struct sockbase *sockbase, long event)
{
	struct connection *c = container_of(sockbase, struct connection, sockbase);
	struct server *s = container_of(c, struct server, c);

	if (event & EVENT_WRITE) {
		// TODO: handle write
		if (c->outbuf_len) {
			int e = write(fd, c->outbuf, c->outbuf_len);
			if (e < 0) {
				sockerror("write()");
				server_close(s);
				return;
			}
			if (e < (int)c->outbuf_len) {
				memmove(c->outbuf, c->outbuf + e, c->outbuf_len - e);
			}
			c->outbuf_len -= e;
			/* if the buffer is empty, clear the write flag */
			if (!c->outbuf_len)
				sockclr(fd, EVENT_WRITE);
		}
	}
	if (event & EVENT_READ) {
		// TODO: handle read
		int rem = (int)c->bufmax - (int)c->buflen;
		fprintf(stderr, "INFO:%s():rem=%d\n", __func__, rem);

		if (rem > 0) {
			int e = read(fd, c->buf + c->buflen, rem);
			if (e < 0) {
				sockerror("read()");
				server_close(s);
				return;
			}
			if (e == 0) {
				fprintf(stderr, "Connection closed\n");
				server_close(s);
				return;
			}
			fprintf(stderr, "INFO:%s():e=%d\n", __func__, e);
			c->buflen += e;
		}
	}
}

static struct sockbase *server_new(SOCKET fd, const char *origin)
{
	struct server *s;
	s = calloc(1, sizeof(*s));
	RETAIN(&s->c.sockbase);
	connection_init(&s->c, fd);

	/* copy the template environment */
	const char *template = obj_get(system_env, "server.template");
	if (template) {
		s->env = objdb_load(template);
	} else {
		fprintf(stderr, "WARNING:server.template not set, using empty environment\n");
		s->env = obj_new();
	}

	/* set environment variable */
	if (obj_set(s->env, "ORIGIN", origin)) {
		struct sockbase *sb = &s->c.sockbase;
		fprintf(stderr, "ERROR:could not create connection\n");
		RELEASE(sb, server_free_sockbase);
		return NULL;
	}

	sockadd(fd, &s->c.sockbase, EVENT_READ, server_event, server_free_sockbase);

	/* show an annoying legal notice */
	connection_printf(&s->c,
		"Copyright 2015 Jon Mayo <jon@cobra-kai.com>\n"
		"\n"
		"This program is free software: you can redistribute it and/or modify it\n"
		"under the terms of the GNU Affero General Public License version 3 as\n"
		"published by the Free Software Foundation supplemented with the\n"
		"Additional Terms, as set forth in the License Agreement for the Waking\n"
		"Well MUD.\n"
		"\n"
		"This program is distributed in the hope that it will be useful, but\n"
		"WITHOUT ANY WARRANTY; without even the implied warranty of\n"
		"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero\n"
		"General Public License for more details.\n"
		"\n"
		"You should have received a copy of the License Agreement for the Waking\n"
		"Well MUD along with this program. If not, see\n"
		"http://www.gnu.org/licenses/agpl-3.0.en.html\n"
		"\n"
		"You are required to keep these \"Appropriate Legal Notices\" intact as\n"
		"set forth in section 5(d) of the GNU Affero General Public License\n"
		"version 3. In accordance with section 7(b) these Legal Notices must\n"
		"retain the display of the \"the Waking Well MUD\" logo in order to\n"
		"indicate the origin of the Program. If the display of the logo is not\n"
		"reasonably feasible for technical reasons, these Legal Notices must\n"
		"display the phrase \"the Waking Well MUD\".\n\n");

	command_run("print", s); // TODO: execute starting object

	return &s->c.sockbase;
}

/******************************************************************************/

/* a service accepts connections from remote client and creates servers */
struct service {
	struct sockbase sockbase;
	struct service *next, **prev;
	struct object *template; // TODO: load this
};

static struct service *service_list;

static void service_close(struct service *s)
{
	sockclose(s->sockbase.fd);
	s->sockbase.fd = -1;
}

void service_free(struct service *s)
{
	service_close(s);
	free(s);
}

static void service_free_sockbase(struct sockbase *base)
{
	struct service *s = container_of(base, struct service, sockbase);
	service_free(s);
}

static void service_event(SOCKET fd, struct sockbase *sockbase, long event)
{
	// TODO: setup global parameters from the service structure:
	struct service *s = container_of(sockbase, struct service, sockbase);

	if (event & EVENT_READ) {
		struct sockaddr_storage sa;
		socklen_t sa_len = sizeof(sa);

		SOCKET newfd = accept4(fd, (struct sockaddr*)&sa, &sa_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
		if (newfd == INVALID_SOCKET) {
			if (errno != EAGAIN)
				sockerror("accept()");
			return;
		}

		/* post <host>/<port> into buffer host[] ... */
		char host[128], port[64];
		int e = getnameinfo((struct sockaddr*)&sa, sa_len,
			host, sizeof(host), port, sizeof(port),
			NI_NUMERICHOST | NI_NUMERICSERV);
		if (e) {
			snprintf(host, sizeof(host), "<UNKNOWN-HOST>");
			snprintf(port, sizeof(port), "<UNKNOWN-PORT>");
		}
		size_t hostlen = strlen(host);
		// TODO: check for truncation in snprintf
		snprintf(host + hostlen, sizeof(host) - hostlen, "/%s", port);

		struct sockbase *newserver = server_new(newfd, host);
		if (!newserver) {
			fprintf(stderr, "ERROR:could not create connection\n");
			sockclose(newfd);
			return;
		}

		fprintf(stderr, "New conncection: %s\n", host);
	}
}

int service_open(const char *hostport)
{
	/* split host and port number from HHHHH/NNNN */
	char host[NI_MAXHOST];
	const char *service = strrchr(hostport, '/');
	if (!service)
		service = hostport;
	snprintf(host, sizeof(host), "%.*s", (int)(service - hostport), hostport);
	if (service)
		service++;

	/* Bind all matching addresses */
	struct addrinfo *res, *cur, hints = {
		.ai_flags = AI_NUMERICHOST | AI_PASSIVE,
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	int e = getaddrinfo(*host ? host : NULL, service, &hints, &res);
	if (e) {
		fprintf(stderr, "ERROR:%s:%s\n", hostport, gai_strerror(e));
		return -1;
	}

	for (cur = res; cur; cur = cur->ai_next) {
		SOCKET fd = socket(cur->ai_family, cur->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, cur->ai_protocol);
		if (fd == INVALID_SOCKET) {
			sockerror(hostport);
			continue;
		}
		/* fcntl(fd, F_SETFL, O_NONBLOCK); */
		/* fcntl(fd, F_SETFD, FD_CLOEXEC); */

		if (bind(fd, cur->ai_addr, cur->ai_addrlen) == -1 ||
				listen(fd, 6) == -1) {
			sockerror(hostport);
			sockclose(fd);
			continue;
		}

		/* add the service */
		struct service *s;
		s = calloc(1, sizeof(*s));
		RETAIN(&s->sockbase); // TODO: write a function to close a service too
		s->sockbase.fd = fd;
		DLIST_INSERT_AFTER(&service_list, s);
		sockadd(fd, &s->sockbase, EVENT_READ, service_event, service_free_sockbase);
		fprintf(stderr, "Started %s\n", hostport);
	}
	freeaddrinfo(res);
	return 0;
}

/******************************************************************************/
// TODO: these functions need a connection and an object
void act_print(void *p)
{
	struct server *s = p;
	fprintf(stderr, "%s():p=%p\n", __func__, p);

	connection_printf(&s->c, "Hello\n");
}

/******************************************************************************/

int main(int argc, char **argv)
{
	setlocale(LC_ALL, NULL);

	/* parse command-line options */
	int e = 0;
	switch (argc) {
	case 1:
		e = objdb_setroot("./db");
		break;
	case 2:
		e = objdb_setroot(argv[1]);
		break;
	default:
		fprintf(stderr, "usage: %s [<dbpath>]\n", basename(argv[0]));
		return EXIT_FAILURE;
	}
	if (e) {
		fprintf(stderr, "unable to configure DB path\n");
		return EXIT_FAILURE;
	}

	/* load enviroment options */
	system_env = objdb_load("system/config");
	if (!system_env) {
		fprintf(stderr, "ERROR:system/config not found.\n");
		return EXIT_FAILURE;
	}

	/* load core commands */
	command_register("print", act_print);

	service_open("/5000"); // TODO: read from system_env
	while (sockets_count > 0) {
		if (sockpoll()) {
			return EXIT_FAILURE;
		}
	}

	obj_release(system_env);
	system_env = NULL;
	return 0;
}
