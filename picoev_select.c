#include <sys/select.h>
#include "picoev.h"

picoev_globals picoev;

void picoev_update_events_internal(picoev_loop* loop, int fd, int events)
{
  picoev.fds[fd].events = events;
}

void picoev_loop_once(picoev_loop* loop)
{
  fd_set readfds, writefds, errorfds;
  int i, maxfd = 0;
  
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
  
  if (maxfd != 0
      && select(maxfd + 1, &readfds, &writefds, &errorfds, NULL) > 0) {
    
    /* handle */
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
  
  // TODO handle timeout
}
