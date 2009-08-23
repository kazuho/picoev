#include <sys/select.h>
#include "picoev.h"

picoev_globals picoev;

int picoev_update_events_internal(picoev_loop* loop, int fd, int events)
{
  picoev.fds[fd].events = events;
  return 0;
}

picoev_loop* picoev_create_loop(int max_timeout)
{
  picoev_loop* loop;
  
  assert(PICOEV_IS_INITED);
  if ((loop = (picoev_loop*)malloc(sizeof(picoev_loop))) == NULL) {
    return NULL;
  }
  if (picoev_init_loop_internal(loop, max_timeout) != 0) {
    free(loop);
    return NULL;
  }
  
  return loop;
}

int picoev_destroy_loop(picoev_loop* loop)
{
  picoev_deinit_loop_internal(loop);
  free(loop);
  return 0;
}

int picoev_poll_once_internal(picoev_loop* loop, int max_wait)
{
  fd_set readfds, writefds, errorfds;
  struct timeval tv;
  int i, r, maxfd = 0;
  
  /* setup */
  FD_ZERO(&readfds);
  FD_ZERO(&writefds);
  FD_ZERO(&errorfds);
  for (i = 0; i < picoev.max_fd; ++i) {
    picoev_fd* fd = picoev.fds + i;
    if (fd->loop_id == loop->loop_id) {
      if ((fd->events & PICOEV_READ) != 0) {
	FD_SET(i, &readfds);
	if (maxfd < i) {
	  maxfd = i;
	}
      }
      if ((fd->events & PICOEV_WRITE) != 0) {
	FD_SET(i, &writefds);
	if (maxfd < i) {
	  maxfd = i;
	}
      }
    }
  }
  
  /* select and handle if any */
  tv.tv_sec = max_wait;
  tv.tv_usec = 0;
  r = select(maxfd + 1, &readfds, &writefds, &errorfds, &tv);
  if (r == -1) {
    return -1;
  } else if (r > 0) {
    for (i = 0; i < picoev.max_fd; ++i) {
      picoev_fd* target = picoev.fds + i;
      if (target->loop_id == loop->loop_id) {
	int revents = (FD_ISSET(i, &readfds) ? PICOEV_READ : 0)
	  | (FD_ISSET(i, &writefds) ? PICOEV_WRITE : 0);
	if (revents != 0) {
	  (*target->callback)(loop, i, revents, target->cb_arg);
	}
      }
    }
  }
  
  return 0;
}
