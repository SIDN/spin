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

	s_un.sun_family = AF_UNIX;
	if (snprintf(s_un.sun_path, sizeof(s_un.sun_path), "%s",
	    sockpath) >= (ssize_t)sizeof(s_un.sun_path)) {
		errx(1, "socket path too long");
	}
	if (connect(fd, (struct sockaddr *)&s_un, sizeof(s_un)) == -1) {
		err(1, "connect");
	}

	return fd;
}

void
socket_writemsg(int fd, char *msg, size_t msg_len)
{
	if (send(fd, msg, msg_len, 0) != msg_len) {
		err(1, "send");
	}
}
