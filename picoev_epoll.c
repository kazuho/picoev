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
  
#define CTL(m, e)			      \
  if (epoll_ctl(loop->epfd, m, fd, e) != 0) { \
    return -1; \
  }
  
  if (old_events == events) {
    return 0;
  }
  if (events != 0) {
    struct epoll_event ev;
    ev.events = ((events & PICOEV_READ) != 0 ? EPOLLIN : 0)
      | ((events & PICOEV_WRITE) != 0 ? EPOLLOUT : 0);
    ev.data.fd = fd;
    if (old_events != 0) {
      CTL(EPOLL_CTL_MOD, &ev);
    } else {
      CTL(EPOLL_CTL_ADD, &ev);
    }
  } else {
    CTL(EPOLL_CTL_DEL, 0);
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
  int i, nevents, timeout_secs;
  
  timeout_secs = loop->loop.timeout.resolution;
  if (max_wait != 0 && max_wait < timeout_secs) {
    timeout_secs = max_wait;
  }
  nevents = epoll_wait(loop->epfd, loop->events,
		       sizeof(loop->events) / sizeof(loop->events[0]),
		       timeout_secs * 1000);
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
