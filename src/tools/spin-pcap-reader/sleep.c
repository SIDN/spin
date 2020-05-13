#include <sys/types.h>
#define __USE_GNU /* for TIMEVAL_TO_TIMESPEC */
#include <sys/time.h>
#undef __USE_GNU

#include <err.h>
#include <errno.h>
#include <time.h>

#include "sleep.h"

static void
monotime(struct timespec *ts)
{
	if (clock_gettime(CLOCK_MONOTONIC, ts) == -1) {
		err(1, "clock_gettime");
	}
}

void
maybe_sleep(const struct timeval *cur_pcap_tv)
{
	static struct timespec last_pcap = { 0, 0 };
	static struct timespec last_wall = { 0, 0 };
	struct timespec cur_pcap;
	struct timespec cur_wall;
	struct timespec diff_pcap;
	struct timespec diff_wall;
	struct timespec to_sleep;
	struct timespec remainder;

	TIMEVAL_TO_TIMESPEC(cur_pcap_tv, &cur_pcap);

	monotime(&cur_wall);

	if (timespecisset(&last_wall)) {
		timespecsub(&cur_pcap, &last_pcap, &diff_pcap);
		timespecsub(&cur_wall, &last_wall, &diff_wall);

		if (timespeccmp(&diff_wall, &diff_pcap, <)) {
			timespecsub(&diff_pcap, &diff_wall, &to_sleep);

			while (nanosleep(&to_sleep, &remainder) == -1) {
				if (errno != EINTR) {
					err(1, "nanosleep");
				}
				to_sleep = remainder;
			}
		}
	}

	last_pcap = cur_pcap;
	last_wall = cur_wall;
}
