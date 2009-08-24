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
#include <sys/epoll.h>
#include <unistd.h>
#include "picoev.h"

typedef struct picoev_loop_epoll_st {
  picoev_loop loop;
  int epfd;
  int changed_fds; /* -1 if none */
  struct epoll_event events[1024];
} picoev_loop_epoll;

#define BACKEND_GET_NEXT_FD(backend) ((backend) >> 8)
#define BACKEND_GET_OLD_EVENTS(backend) ((char)backend)
#define BACKEND_BUILD(nextfd, oldevents) (((nextfd) << 8) | (oldevents))

picoev_globals picoev;

__inline void picoev_call_epoll(picoev_loop_epoll* loop, int fd,
				picoev_fd* target)
{
  if (loop->loop.loop_id != target->loop_id) {
    /* now used by another thread, disable */
    epoll_ctl(loop->epfd, EPOLL_CTL_DEL, fd, 0);
  } else if (target->_backend != -1) {
    /* apply changes even if the old and new flags are equivalent, if the
       socket was reopened, it might require re-assigning */
    int old_events = BACKEND_GET_OLD_EVENTS(target->_backend);
    if (old_events != 0 && target->events == 0) {
      epoll_ctl(loop->epfd, EPOLL_CTL_DEL, fd, 0);
    } else {
      struct epoll_event ev;
      int r;
      ev.events = ((target->events & PICOEV_READ) != 0 ? EPOLLIN : 0)
	| ((target->events & PICOEV_WRITE) != 0 ? EPOLLOUT : 0);
      ev.data.fd = fd;
      if (old_events != 0) {
	if (epoll_ctl(loop->epfd, EPOLL_CTL_MOD, fd, &ev) != 0) {
	  assert(errno == ENOENT);
	  r = epoll_ctl(loop->epfd, EPOLL_CTL_ADD, fd, &ev);
	  assert(r == 0);
	}
      } else {
	if (epoll_ctl(loop->epfd, EPOLL_CTL_ADD, fd, &ev) != 0) {
	  assert(errno == EEXIST);
	  r = epoll_ctl(loop->epfd, EPOLL_CTL_MOD, fd, &ev);
	  assert(r == 0);
	}
      }
    }
  }
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
  
  /* init myself */
  if ((loop->epfd = epoll_create(picoev.max_fd)) == -1) {
    picoev_deinit_loop_internal(&loop->loop);
    free(loop);
    return NULL;
  }
  loop->changed_fds = -1;
  
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

int picoev_init_backend()
{
  int i;
  
  for (i = 0; i < picoev.max_fd; ++i) {
    picoev.fds[i]._backend = -1;
  }
  
  return 0;
}

int picoev_deinit_backend()
{
  return 0;
}

int picoev_update_events_internal(picoev_loop* _loop, int fd, int events)
{
  picoev_loop_epoll* loop = (picoev_loop_epoll*)_loop;
  picoev_fd* target = picoev.fds + fd;
  
  assert(PICOEV_FD_BELONGS_TO_LOOP(&loop->loop, fd));
  
  if (target->events == events) {
    return 0;
  }
  
  /* update chain */
  if (target->_backend == -1) {
    target->_backend = BACKEND_BUILD(loop->changed_fds, target->events);
    loop->changed_fds = fd;
  }
  target->events = events;
  
  return 0;
}

int picoev_poll_once_internal(picoev_loop* _loop, int max_wait)
{
  picoev_loop_epoll* loop = (picoev_loop_epoll*)_loop;
  int i, nevents;
  
  if (loop->changed_fds != -1) {
    int fd = loop->changed_fds;
    do {
      picoev_fd* target = picoev.fds + fd;
      picoev_call_epoll(loop, fd, target);
      fd = BACKEND_GET_NEXT_FD(target->_backend);
      target->_backend = -1;
    } while (fd != -1);
    loop->changed_fds = -1;
  }
  
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
    } else {
      epoll_ctl(loop->epfd, EPOLL_CTL_DEL, event->data.fd, 0);
    }
  }
  return 0;
}
