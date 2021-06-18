# External source (extsrc)

## Introduction

*External source* (extsrc) is a facility in SPIN that allows
an external program to feed information to spind through a socket.
The idea is that this facility can be used for simulating network traffic
or for testing spind, for example.

spind either opens a UNIX domain socket on `EXTSRC_SOCKET_PATH`
(currently defined as `/var/run/spin-extsrc.sock`)
or an Internet socket on port `EXTSRC_PORT`.
Applications can send messages through this socket;
see `src/include/extsrc.h` for more details.
Applications can use the `extsrc_msg_create_*` functions to create messages
that can be sent through the socket.

The spin-pcap-reader,
found in `src/tools/spin-pcap-reader/`,
is a user of the extsrc interface.

## Location of the code

The spind component of extsrc can be found in the following files:

- `src/include/extsrc.h`
- `src/spind/core2extsrc.c`
- `src/spind/core2extsrc.h`

And the ''external program''-side of extsrc can be found in the following files:

- `src/include/extsrc.h`
- `src/lib/extsrc.c`

Currently, some code which can be used by an application to connect to the
socket and send messages to the socket resides in the following files:

- `src/tools/spin-pcap-reader/socket.c`
- `src/tools/spin-pcap-reader/socket.h`

In the future,
we may want to move this code to `src/lib/`.

