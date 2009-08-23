#include <sys/select.h>
#include "picoev.h"

picoev_globals picoev;

void picoev_update_events_internal(picoev_loop* loop, int fd, int events)
{
  picoev.fds[fd].events = events;
}

void picoev_loop_once(picoev_loop* loop, int max_wait)
{
  fd_set readfds, writefds, errorfds;
  struct timeval tv;
  int i, maxfd = 0;
  
  loop->now = time(NULL);
  
  /* setup */
  FD_ZERO(&readfds);
  FD_ZERO(&writefds);
  FD_ZERO(&errorfds);
  for (i = 0; i < picoev.max_fd; ++i) {
    picoev_fd* fd = picoev.fds + i;
    if (fd->loop_id == loop->loop_id) {
      if ((fd->events & (PICOEV_READ | PICOEV_ACCEPT)) != 0) {
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
  tv.tv_sec = loop->timeout.resolution;
  if (max_wait != 0 && max_wait < tv.tv_sec) {
    tv.tv_sec = max_wait;
  }
  tv.tv_usec = 0;
  if (select(maxfd + 1, &readfds, &writefds, &errorfds, &tv) > 0) {
    for (i = 0; i < picoev.max_fd; ++i) {
      picoev_fd* fd = picoev.fds + i;
      if (fd->loop_id == loop->loop_id) {
	int revents = (FD_ISSET(i, &readfds) ? PICOEV_READ : 0)
	  | (FD_ISSET(i, &writefds) ? PICOEV_WRITE : 0);
	if (revents != 0) {
	  (*fd->callback)(loop, i, revents, fd->cb_arg);
	}
      }
    }
  }
  
  /* handle timeout */
  picoev_handle_timeout_internal(loop);
}
