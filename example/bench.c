/*
 * Copyright 2003 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Mon 03/10/2003 - Modified by Davide Libenzi <davidel@xmailserver.org>
 *
 *     Added chain event propagation to improve the sensitivity of
 *     the measure respect to the event loop efficency.
 *
 *
 */

#define	timersub(tvp, uvp, vvp)						\
	do {								\
		(vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;		\
		(vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec;	\
		if ((vvp)->tv_usec < 0) {				\
			(vvp)->tv_sec--;				\
			(vvp)->tv_usec += 1000000;			\
		}							\
	} while (0)
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#if PICOEV
# include "picoev.h"
picoev_loop* pe_loop;
#endif
#if NATIVE
# include "ev.h"
#endif
#include <event.h>


static int count, writes, fired;
static int *pipes;
static int num_pipes, num_active, num_writes;
static struct event *events;
static int timers, native;
static struct ev_io *evio;
static struct ev_timer *evto;



void
read_cb(int fd, short which, void *arg)
{
	int idx = (int) arg, widx = idx + 1;
	u_char ch;

        if (timers)
          {
	    if (native == 2) {
#if PICOEV
	      picoev_set_timeout(pe_loop, fd, 10);
	      drand48();
#else
	      abort();
#endif
	    } else if (native)
              {
#if NATIVE
                evto [idx].repeat = 10. + drand48 ();
                ev_timer_again (&evto [idx]);
#else
                abort ();
#endif
              }
            else
              {
                struct timeval tv;
                event_del (&events [idx]);
                tv.tv_sec  = 10;
                tv.tv_usec = drand48() * 1e6;
                event_add(&events[idx], &tv);
              }
          }

	count += read(fd, &ch, sizeof(ch));
	if (writes) {
		if (widx >= num_pipes)
			widx -= num_pipes;
		write(pipes[2 * widx + 1], "e", 1);
		writes--;
		fired++;
	}
}

#if PICOEV
void
cb_picoev(picoev_loop* loop, int fd, int revents, void* cb_arg)
{
  read_cb(fd, revents, cb_arg);
}
#endif

#if NATIVE
void
read_thunk(struct ev_io *w, int revents)
{
  read_cb (w->fd, revents, w->data);
}

void
timer_cb (struct ev_timer *w, int revents)
{
  assert(0);
}
#endif

struct timeval *
run_once(void)
{
	int *cp, i, space;
	static struct timeval ta, ts, te, tv;

	gettimeofday(&ta, NULL);
	for (cp = pipes, i = 0; i < num_pipes; i++, cp += 2) {
	  if (native == 2) {
#if PICOEV
	    if (picoev_is_active(pe_loop, cp[0])) {
	      picoev_del(pe_loop, cp[0]);
	    }
	    picoev_add(pe_loop, cp[0], PICOEV_READ, 10, cb_picoev, (void*)i);
	    drand48();
#else
	    abort();
#endif
	  } else if (native)
            {
#if NATIVE
              if (ev_is_active (&evio [i]))
                ev_io_stop (&evio [i]);

              ev_io_set (&evio [i], cp [0], EV_READ);
              ev_io_start (&evio [i]);

              evto [i].repeat = 10. + drand48 ();
              ev_timer_again (&evto [i]);
#else
              abort ();
#endif
            }
          else
            {
		event_del(&events[i]);
		event_set(&events[i], cp[0], EV_READ | EV_PERSIST, read_cb, (void *) i);
                tv.tv_sec  = 10.;
                tv.tv_usec = drand48() * 1e6;
		event_add(&events[i], timers ? &tv : 0);
            }
	}

#if PICOEV
	if (native == 2) {
	  picoev_loop_once(pe_loop, 0);
	} else
#endif
	event_loop(EVLOOP_ONCE | EVLOOP_NONBLOCK);

	fired = 0;
	space = num_pipes / num_active;
	space = space * 2;
	for (i = 0; i < num_active; i++, fired++)
		write(pipes[i * space + 1], "e", 1);

	count = 0;
	writes = num_writes;
	{ int xcount = 0;
	gettimeofday(&ts, NULL);
	do {
#if PICOEV
	  if (native == 2) {
	    picoev_loop_once(pe_loop, 0);
	  } else
#endif
	        event_loop(EVLOOP_ONCE | EVLOOP_NONBLOCK);
		xcount++;
	} while (count != fired);
	gettimeofday(&te, NULL);

	//if (xcount != count) fprintf(stderr, "Xcount: %d, Rcount: %d\n", xcount, count);
	}

	timersub(&te, &ta, &ta);
	timersub(&te, &ts, &ts);
		fprintf(stdout, "%8ld %8ld\n",
			ta.tv_sec * 1000000L + ta.tv_usec,
			ts.tv_sec * 1000000L + ts.tv_usec
                        );

	return (&te);
}

int
main (int argc, char **argv)
{
	struct rlimit rl;
	int i, c;
	struct timeval *tv;
	int *cp;
	extern char *optarg;

	num_pipes = 100;
	num_active = 1;
	num_writes = num_pipes;
	while ((c = getopt(argc, argv, "n:a:w:te:")) != -1) {
		switch (c) {
		case 'n':
			num_pipes = atoi(optarg);
			break;
		case 'a':
			num_active = atoi(optarg);
			break;
		case 'w':
			num_writes = atoi(optarg);
			break;
		case 'e':
#if PICOEV
		  if (strcmp(optarg, "picoev") == 0) {
		    native = 2;
		  } else
#endif
                        native = 1;
			break;
		case 't':
                        timers = 1;
			break;
		default:
			fprintf(stderr, "Illegal argument \"%c\"\n", c);
			exit(1);
		}
	}

#if 1
	rl.rlim_cur = rl.rlim_max = num_pipes * 2 + 50;
	if (setrlimit(RLIMIT_NOFILE, &rl) == -1) {
		perror("setrlimit");
	}
#endif

#if PICOEV
	picoev_init(num_pipes * 2 + 10);
	pe_loop = picoev_create_loop(60);
#endif
#if NATIVE
	evio = calloc(num_pipes, sizeof(struct ev_io));
	evto = calloc(num_pipes, sizeof(struct ev_timer));
#endif
	events = calloc(num_pipes, sizeof(struct event));
	pipes = calloc(num_pipes * 2, sizeof(int));
	if (events == NULL || pipes == NULL) {
		perror("malloc");
		exit(1);
	}

	event_init();

	for (cp = pipes, i = 0; i < num_pipes; i++, cp += 2) {
#if NATIVE
          if (native) {
            ev_init (&evto [i], timer_cb);
            ev_init (&evio [i], read_thunk);
            evio [i].data = (void *)i;
          }
#endif
#ifdef USE_PIPES
		if (pipe(cp) == -1) {
#else
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, cp) == -1) {
#endif
			perror("pipe");
			exit(1);
		}
	}

	for (i = 0; i < 2; i++) {
		tv = run_once();
	}

	exit(0);
}
