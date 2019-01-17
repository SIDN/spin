#include "mainloop.h"
#include "spin_log.h"

#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <errno.h>

/*
 * Subsystems can register here for the mainloop
 * Mainloop maintains list of file descriptors and timeouts
 * Either or both can be specified
 *
 * Administration of work functions
 *
 * File descriptors must be non-zero and unique
 */

#define MAXMNR 10	/* More than this would be excessive */
static
struct mnreg {
    workfunc		mnr_wf;		/* The to-be-called work function */
    int			mnr_fd;		/* File descriptor if non zero */
    int			mnr_pollnumber; /* Index in poll() list if >= 0 */
    struct timeval	mnr_toval;	/* Periodic timeouts so often */
    struct timeval	mnr_nxttime;	/* Time of next end-of-period */
} mnr[MAXMNR];
static int n_mnr = 0;

static void panic(char *s) {

    spin_log(LOG_ERR, "Fatal error: %s\n", s);
    exit(-1);
}

void mainloop_register(workfunc wf, int fd, int toval) {
    int i;

    if (n_mnr >= MAXMNR) {
	panic("Ran out of MNR structs");
    }
    if (fd  != 0) {
	/* File descriptors if non-zero must be unique */
	for (i=0; i<n_mnr; i++) {
	    if (mnr[i].mnr_fd == fd) {
		panic("Non unique fd");
	    }
	}
    }
    mnr[n_mnr].mnr_wf = wf;
    mnr[n_mnr].mnr_fd = fd;
    mnr[n_mnr].mnr_toval.tv_sec = 0;
    mnr[n_mnr].mnr_toval.tv_usec = 1000*toval;
    n_mnr++;
}

static void init_mltime() {
    struct timeval tvstart;
    int i;

    gettimeofday(&tvstart, 0);
    for (i=0; i<n_mnr; i++) {
	if (timerisset(&mnr[i].mnr_toval)) {
	    timeradd(&tvstart, &mnr[i].mnr_toval, &mnr[i].mnr_nxttime);
	} else {
	    timerclear(&mnr[i].mnr_nxttime);
	}
    }
}

static int mainloop_findtime(struct timeval *tnow, struct timeval *tint) {
    struct timeval posnext, onesecond, earliest, interval;
    int i;
    int millitime;

    gettimeofday(tnow, 0);
    onesecond.tv_sec = 1;
    onesecond.tv_usec = 0;
    timeradd(tnow, &onesecond, &earliest);
    for (i=0; i<n_mnr; i++) {
	if (timerisset(&mnr[i].mnr_toval)) {
	    timeradd(tnow, &mnr[i].mnr_toval, &posnext);
	    if (timercmp(&posnext, &earliest, <)) {
		earliest = posnext;
	    }
	}
    }
    *tint = earliest;
    timersub(&earliest, tnow, &interval);
    if (interval.tv_sec > 10) {
	millitime = 1000*interval.tv_sec;
    } else {
	millitime = (interval.tv_usec+999)/1000;
    }
    return millitime ? millitime : 1;
}

static int mainloop_running = 1;
void mainloop_end() {

    if (mainloop_running) {
        spin_log(LOG_INFO, "Got interrupt, quitting\n");
        mainloop_running = 0;
    } else {
        spin_log(LOG_INFO, "Got interrupt again, hard exit\n");
        exit(0);
    }
}

void mainloop_run() {
    struct pollfd fds[MAXMNR];
    int nfds = 0;
    int i, pollnum;
    int rs;
    struct timeval time_now, time_interesting;
    int millitime;
    int argdata, argtmout;

    init_mltime();
    for (i=0; i<n_mnr; i++) {
	if (mnr[i].mnr_fd) {
	    mnr[i].mnr_pollnumber = nfds;
	    fds[nfds].fd = mnr[i].mnr_fd;
	    fds[nfds].events = POLLIN;
	    nfds++;
	} else {
	    mnr[i].mnr_pollnumber = -1;
	}
    }

    while (mainloop_running) {
	millitime = mainloop_findtime(&time_now, &time_interesting);

	// go poll and wait until something interesting is up
	rs = poll(fds, nfds, millitime);

	if (rs < 0) {
	    spin_log(LOG_ERR, "error in poll(): %s\n", strerror(errno));
	    continue; // IS this right??
	}

	// find out what is due next

	gettimeofday(&time_now, 0);
	for (i=0; i<n_mnr; i++) {
	    argdata = 0;
	    argtmout = 0;
	    if (timercmp(&time_now, &mnr[i].mnr_nxttime, >)) {
		// Timer of this event went off
		argtmout = 1;
	    }
	    pollnum = mnr[i].mnr_pollnumber ;
	    if ( pollnum >= 0) {
		if (fds[pollnum].revents & POLLIN) {
		    argdata = 1;
		}
	    }
	    if (argdata || argtmout)
		(*mnr[i].mnr_wf)(argdata, argtmout);
	}
    }
}

