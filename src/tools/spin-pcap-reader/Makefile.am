
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g -Wall -Werror

bin_PROGRAMS = spin-pcap-reader
spin_pcap_reader_SOURCES = arpupdate.c \
                           ipt.c \
                           macstr.c \
                           pcap.c \
                           sleep.c \
                           socket.c \
                           spinhook.c
spin_pcap_reader_CFLAGS =
spin_pcap_reader_LDADD = $(top_builddir)/lib/libspin.a

