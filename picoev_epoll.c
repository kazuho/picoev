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

#include <sys/epoll.h>
#include <unistd.h>
#include "picoev.h"

typedef struct picoev_loop_epoll_st {
  picoev_loop loop;
  int epfd;
  struct epoll_event events[1024];
} picoev_loop_epoll;

picoev_globals picoev;

int picoev_update_events_internal(picoev_loop* _loop, int fd, int events)
{
  picoev_loop_epoll* loop = (picoev_loop_epoll*)_loop;
  int old_events = picoev.fds[fd].events;
  
  assert(PICOEV_FD_BELONGS_TO_LOOP(&loop->loop, fd));
  
#define CTL(m, e)			      \
  if (epoll_ctl(loop->epfd, m, fd, e) != 0) { \
    return -1; \
  }
  
  if (events == 0) {
    CTL(EPOLL_CTL_DEL, 0);
  } else {
    struct epoll_event ev;
    ev.events = ((events & PICOEV_READ) != 0 ? EPOLLIN : 0)
      | ((events & PICOEV_WRITE) != 0 ? EPOLLOUT : 0);
    ev.data.fd = fd;
    if (old_events != 0) {
      CTL(EPOLL_CTL_MOD, &ev);
    } else {
      CTL(EPOLL_CTL_ADD, &ev);
    }
  }
  
#undef CTL
  
  picoev.fds[fd].events = events;
  return 0;
}

picoev_loop* picoev_create_loop(int max_timeout)
{
  picoev_loop_epoll* loop;
  
  /* init parent */
  assert(PICOEV_IS_INITED);
  if ((loop = (picoev_loop_epoll*)malloc(sizeof(picoev_loop_epoll))) == NULL) {
    return NULL;
  }
  if (picoev_init_loop_internal(&loop->loop, max_timeout) != 0) {
    free(loop);
    return NULL;
  }
  
  /* init epoll */
  if ((loop->epfd = epoll_create(picoev.max_fd)) == -1) {
    picoev_deinit_loop_internal(&loop->loop);
    free(loop);
    return NULL;
  }
  
  return &loop->loop;
}

int picoev_destroy_loop(picoev_loop* _loop)
{
  picoev_loop_epoll* loop = (picoev_loop_epoll*)_loop;
  
  if (close(loop->epfd) != 0) {
    return -1;
  }
  picoev_deinit_loop_internal(&loop->loop);
  free(loop);
  return 0;
}

int picoev_poll_once_internal(picoev_loop* _loop, int max_wait)
{
  picoev_loop_epoll* loop = (picoev_loop_epoll*)_loop;
  int i, nevents;
  
  nevents = epoll_wait(loop->epfd, loop->events,
		       sizeof(loop->events) / sizeof(loop->events[0]),
		       max_wait * 1000);
  if (nevents == -1) {
    return -1;
  }
  for (i = 0; i < nevents; ++i) {
    struct epoll_event* event = loop->events + i;
    picoev_fd* target = picoev.fds + event->data.fd;
    if (loop->loop.loop_id == target->loop_id
	&& (event->events & (EPOLLIN | EPOLLOUT)) != 0) {
      int revents = ((event->events & EPOLLIN) != 0 ? PICOEV_READ : 0)
	| ((event->events & EPOLLOUT) != 0 ? PICOEV_WRITE : 0);
      (*target->callback)(&loop->loop, event->data.fd, revents,
			  target->cb_arg);
    }
  }
  return 0;
}
