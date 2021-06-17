#include <sys/socket.h>
#include <sys/un.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>

#include "extsrc.h"
#include "socket.h"

int
socket_open(const char *sockpath)
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
	    sockpath) >= (ssize_t)sizeof(s_un.sun_path)) {
		errx(1, "%s: socket path too long", sockpath);
	}
	if (connect(fd, (struct sockaddr *)&s_un, sizeof(s_un)) == -1) {
		err(1, "connect: %s", sockpath);
	}

	return fd;
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
