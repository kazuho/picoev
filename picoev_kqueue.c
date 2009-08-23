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

int picoev_update_events_internal(picoev_loop* _loop, int fd, int events)
{
  picoev_loop_kqueue* loop = (picoev_loop_kqueue*)_loop;
  
  assert(PICOEV_FD_BELONGS_TO_LOOP(&loop->loop, fd));
  
#define SET(ev, cmd)					  \
  EV_SET(loop->ev_queue + loop->ev_queue_off++, fd,	  \
	 ((ev & PICOEV_READ) != 0 ? EVFILT_READ : 0)	  \
	 | ((ev & PICOEV_WRITE) != 0 ? EVFILT_WRITE : 0), \
	 cmd, 0, 0, NULL)
  
  if (picoev.fds[fd].events != 0) {
    SET(picoev.fds[fd].events, EV_ADD | EV_ENABLE);
  }
  if (events != 0) {
    SET(events, EV_ADD);
  }
  
#undef SET 
  
  /* should call imediately if the user might be going to close the socket */
  if (events == 0 || loop->ev_queue_off + 2 >= EV_QUEUE_SZ) {
    int r = kevent(loop->kq, loop->ev_queue, loop->ev_queue_off, NULL, 0, NULL);
    assert(r == 0);
    loop->ev_queue_off = 0;
  }
  
  picoev.fds[fd].events = events;
  return 0;
}

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
  
