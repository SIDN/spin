#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <err.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>

#include "extsrc.h"
#include "socket.h"

/*
 * If not using UNIX domain sockets, use TCP for communication between
 * an extsrc client and spind if defined; UDP otherwise.
 */
#define EXTSRC_TCP

static int
socket_open_inet(const char *host)
{
	struct addrinfo hints, *res, *res0;
	char port[sizeof("65535")];
	int error;
	int save_errno;
	int fd;
	const char *cause = NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
#ifdef EXTSRC_TCP
	hints.ai_socktype = SOCK_STREAM;
#else
	hints.ai_socktype = SOCK_DGRAM;
#endif
	snprintf(port, sizeof(port), "%d", EXTSRC_PORT);
	error = getaddrinfo(host, port, &hints, &res0);
	if (error) {
		errx(1, "getaddrinfo: %s", gai_strerror(error));
	}
	fd = -1;
	for (res = res0; res; res = res->ai_next) {
		fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (fd == -1) {
			cause = "socket";
			continue;
		}

		if (connect(fd, res->ai_addr, res->ai_addrlen) == -1) {
			cause = "connect";
			save_errno = errno;
			close(fd);
			errno = save_errno;
			fd = -1;
			continue;
		}

		break;
	}
	if (fd == -1) {
		err(1, "%s", cause);
	}

	freeaddrinfo(res0);

	return fd;
}

static int
socket_open_unix(const char *path)
{
	int fd;
	struct sockaddr_un s_un;

	fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd == -1) {
		err(1, "socket");
	}

	memset(&s_un, 0, sizeof(s_un));
	s_un.sun_family = AF_UNIX;
	if (snprintf(s_un.sun_path, sizeof(s_un.sun_path), "%s",
	    path) >= (ssize_t)sizeof(s_un.sun_path)) {
		errx(1, "%s: socket path too long", path);
	}
	if (connect(fd, (struct sockaddr *)&s_un, sizeof(s_un)) == -1) {
		err(1, "connect: %s", path);
	}

	return fd;
}

int
socket_open(const char *path, const char *host)
{
	if (host) {
		return socket_open_inet(host);
	}
	return socket_open_unix(path == NULL ? EXTSRC_SOCKET_PATH : path);
}

void
socket_writemsg(int fd, char *msg, size_t msg_len)
{
	static unsigned long ok = 0;
	static unsigned long fail = 0;

	if (send(fd, msg, msg_len, 0) == -1) {
		if (errno == ENOBUFS) {
			/*
			 * XXX perhaps distinguish between the case where we
			 * read a PCAP and the case where we're listening to an
			 * interface? In the former case we could perhaps wait,
			 * e.g. using poll().
			 */
			++fail;
			warnx("send returned ENOBUFS; %lu/%lu fail/ok", fail,
			    ok);
		} else {
			err(1, "send");
		}
	} else {
		++ok;
	}
}
