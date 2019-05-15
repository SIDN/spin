
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = ${regular_CFLAGS} -g -Wall -Werror

bin_PROGRAMS = pcap-reader
pcap_reader_SOURCES = pcap.c socket.c
pcap_reader_CFLAGS =
pcap_reader_LDADD = $(top_builddir)/lib/libspin.a

