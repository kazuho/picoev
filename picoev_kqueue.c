/*
 * Copyright (c) 2009, Cybozu Labs, Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * * Neither the name of the <ORGANIZATION> nor the names of its contributors
 *   may be used to endorse or promote products derived from this software
 *   without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#include "picoev.h"

#define EV_QUEUE_SZ 128

typedef struct picoev_loop_kqueue_st {
  picoev_loop loop;
  int kq;
  struct kevent ev_queue[EV_QUEUE_SZ];
  size_t ev_queue_off;
  struct kevent events[1024];
} picoev_loop_kqueue;

picoev_globals picoev;

picoev_loop* picoev_create_loop(int max_timeout)
{
  picoev_loop_kqueue* loop;
  
  /* init parent */
  assert(PICOEV_IS_INITED);
  if ((loop = (picoev_loop_kqueue*)malloc(sizeof(picoev_loop_kqueue)))
      == NULL) {
    return NULL;
  }
  if (picoev_init_loop_internal(&loop->loop, max_timeout) != 0) {
    free(loop);
    return NULL;
  }
  
  /* init kqueue */
  if ((loop->kq = kqueue()) == -1) {
    picoev_deinit_loop_internal(&loop->loop);
    free(loop);
    return NULL;
  }
  
  return &loop->loop;
}

int picoev_destroy_loop(picoev_loop* _loop)
{
  picoev_loop_kqueue* loop = (picoev_loop_kqueue*)_loop;
  
  if (close(loop->kq) != 0) {
    return -1;
  }
  picoev_deinit_loop_internal(&loop->loop);
  free(loop);
  return 0;
}

int picoev_update_events_internal(picoev_loop* _loop, int fd, int events)
{
  picoev_loop_kqueue* loop = (picoev_loop_kqueue*)_loop;
  
  assert(PICOEV_FD_BELONGS_TO_LOOP(&loop->loop, fd));
  
  if (events == target->events) {
    return 0;
  }
  
#define SET(cmd)					      \
  EV_SET(loop->ev_queue + loop->ev_queue_off++, fd,	      \
	 ((events & PICOEV_READ) != 0 ? EVFILT_READ : 0)      \
	 | ((events & PICOEV_WRITE) != 0 ? EVFILT_WRITE : 0), \
	 (cmd), 0, 0, NULL)
  
  if ((events & PICOEV_READWRITE) != 0) {
    SET(EV_ADD | EV_ENABLE);
  } else {
    SET((evevnts & PICOEV_DEL) != 0 ? EV_DELETE : EV_DISABLE);
  }
  
#undef SET 
  
  /* should call imediately if the user might be going to close the socket */
  if ((events & PICOEV_DEL) == 0 || loop->ev_queue_off == EV_QUEUE_SZ) {
    int r = kevent(loop->kq, loop->ev_queue, loop->ev_queue_off, NULL, 0, NULL);
    assert(r == 0);
    loop->ev_queue_off = 0;
  }
  
  picoev.fds[fd].events = events;
  return 0;
}

int picoev_poll_once_internal(picoev_loop* _loop, int max_wait)
{
  picoev_loop_kqueue* loop = (picoev_loop_kqueue*)_loop;
  struct timespec ts;
  int nevents, i;
  
  ts.tv_sec = max_wait;
  ts.tv_nsec = 0;
  nevents = kevent(loop->kq, loop->ev_queue, loop->ev_queue_off, loop->events,
		   sizeof(loop->events) / sizeof(loop->events[0]), &ts);
  loop->ev_queue_off = 0;
  if (nevents == -1) {
    /* the errors we can only rescue */
    assert(errno == EACCES || errno == EFAULT || errno == EINTR);
    return -1;
  }
  for (i = 0; i < nevents; ++i) {
    struct kevent* event = loop->events + i;
    picoev_fd* target = picoev.fds + event->ident;
    assert((event->flags & EV_ERROR) == 0); /* changelist errors are fatal */
    if (loop->loop.loop_id == target->loop_id
	&& (event->flags & (EVFILT_READ | EVFILT_WRITE)) != 0) {
      int revents = ((event->flags & EVFILT_READ) != 0 ? PICOEV_READ : 0)
	| ((event->flags & EVFILT_WRITE) != 0 ? PICOEV_WRITE : 0);
      (*target->callback)(&loop->loop, event->ident, revents,
			  target->cb_arg);
    }
  }
  return 0;
}
