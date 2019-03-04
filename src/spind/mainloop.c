#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <errno.h>

#include "mainloop.h"
#include "spin_log.h"
#include "statistics.h"

/*
 * Subsystems can register here for the mainloop
 * Mainloop maintains list of file descriptors and timeouts
 * Either or both can be specified
 *
 * Administration of work functions
 *
 * File descriptors must be non-zero and unique
 */

#define MAXMNR 10       /* More than this would be excessive */
static
struct mnreg {
    char *              mnr_name;       /* Name of module for debugging */
    workfunc            mnr_wf;         /* The to-be-called work function */
    void *              mnr_wfarg;      /* Call back argument */
    int                 mnr_fd;         /* File descriptor if non zero */
    int                 mnr_pollnumber; /* Index in poll() list if >= 0 */
    struct timeval      mnr_toval;      /* Periodic timeouts so often */
    struct timeval      mnr_nxttime;    /* Time of next end-of-period */
} mnr[MAXMNR];
static int n_mnr = 0;

STAT_MODULE(mainloop)

static void panic(char *s) {

    spin_log(LOG_ERR, "Fatal error: %s\n", s);
    exit(-1);
}

// Register work function:  timeout in millisec
void mainloop_register(char *name, workfunc wf, void *arg, int fd, int toval) {
    int i;

    spin_log(LOG_DEBUG, "Mainloop registered %s(..., %d, %d)\n", name, fd, toval);
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
    mnr[n_mnr].mnr_name = name;
    mnr[n_mnr].mnr_wf = wf;
    mnr[n_mnr].mnr_wfarg = arg;
    mnr[n_mnr].mnr_fd = fd;
    /* Convert millisecs to secs and microsecs */
    mnr[n_mnr].mnr_toval.tv_sec = toval/1000;
    mnr[n_mnr].mnr_toval.tv_usec = 1000*(toval%1000);
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

/*
 * Find amount of time to wait to have some timeout
 * At most one second, at least one millisecond
 *
 * Arguments seemed interesting, actually unused (TODO)
 */
static int mainloop_findtime(struct timeval *tnow, struct timeval *tint) {
    struct timeval onesecond, earliest, interval;
    int i;
    int millitime;

    gettimeofday(tnow, 0);
    onesecond.tv_sec = 0;
    onesecond.tv_usec = 999999;
    timeradd(tnow, &onesecond, &earliest);
    for (i=0; i<n_mnr; i++) {
        if (timerisset(&mnr[i].mnr_nxttime)) {
            if (timercmp(&mnr[i].mnr_nxttime, &earliest, <)) {
                earliest = mnr[i].mnr_nxttime;
            }
        }
    }
    *tint = earliest;
    timersub(&earliest, tnow, &interval);

    /* Interval guaranteed to be less than 1 second */
    millitime = (interval.tv_usec+999)/1000;
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

static void
wf_mainloop(void *arg, int data, int timeout) {
    int i;

    spin_log(LOG_DEBUG, "Mainloop table\n");
    for (i=0; i<n_mnr; i++) {
        spin_log(LOG_DEBUG, "MLE: %s %d(%d) %d\n", mnr[i].mnr_name, mnr[i].mnr_pollnumber, mnr[i].mnr_fd, mnr[i].mnr_nxttime.tv_usec);
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
    STAT_COUNTER(ctr, polltime, STAT_TOTAL);

    mainloop_register("mainloop", wf_mainloop, (void *) 0, 0, 10000);
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

        STAT_VALUE(ctr, millitime);
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
            if ( timerisset(&mnr[i].mnr_nxttime) && timercmp(&time_now, &mnr[i].mnr_nxttime, >)) {
                // Timer of this event went off
                argtmout = 1;

                // Increase for next time
                // timeradd(&mnr[i].mnr_nxttime, &mnr[i].mnr_toval, &mnr[i].mnr_nxttime);
                timeradd(&time_now, &mnr[i].mnr_toval, &mnr[i].mnr_nxttime);
            }
            pollnum = mnr[i].mnr_pollnumber ;
            if ( pollnum >= 0) {
                if (fds[pollnum].revents & POLLIN) {
                    argdata = 1;
                }
            }
            if (argdata || argtmout) {
                spin_log(LOG_DEBUG, "Mainloop calling %s (%d, %d)\n", mnr[i].mnr_name, argdata, argtmout);
                (*mnr[i].mnr_wf)(mnr[i].mnr_wfarg, argdata, argtmout);
            }
        }
    }
}
