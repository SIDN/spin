#include <sys/types.h>
#define __USE_GNU /* for TIMESPEC_TO_TIMEVAL */
#include <sys/time.h>
#undef __USE_GNU

#include <err.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "sleep.h"

static void
monotime(struct timeval *tv)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
		err(1, "clock_gettime");
	}

	TIMESPEC_TO_TIMEVAL(tv, &ts);
}

void
maybe_sleep(const struct timeval *cur_pcap)
{
	static struct timeval last_pcap = { 0, 0 };
	static struct timeval last_wall = { 0, 0 };
	struct timeval cur_wall;
	struct timeval diff_pcap;
	struct timeval diff_wall;
	struct timeval to_sleep;

	monotime(&cur_wall);

	if (timerisset(&last_wall)) {
		timersub(cur_pcap, &last_pcap, &diff_pcap);
		timersub(&cur_wall, &last_wall, &diff_wall);

		if (timercmp(&diff_wall, &diff_pcap, <)) {
			timersub(&diff_pcap, &diff_wall, &to_sleep);

			if (select(0, NULL, NULL, NULL, &to_sleep) == -1 &&
			    errno != EINTR) {
				err(1, "select");
			}
		}
	}

	memcpy(&last_pcap, cur_pcap, sizeof(last_pcap));
	memcpy(&last_wall, &cur_wall, sizeof(last_wall));
}
