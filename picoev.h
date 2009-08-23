#ifndef picoev_h
#define picoev_h

#ifdef __cplusplus
extern "C" {
# define PICOEV_INLINE inline
#else
  /* http://gmplib.org/list-archives/gmp-discuss/2008-March/003089.html */
# define PICOEV_INLINE extern __inline__
#endif

#include <assert.h>
#include <stdlib.h>

#define PICOEV_IS_INITED (picoev.max_fd != 0)  
#define PICOEV_IS_INITED_AND_FD_IN_RANGE(fd) ((fd) < picoev.max_fd)
#define PICOEV_NO_MEMORY(p) ((p) != NULL)
#define PICOEV_TOO_MANY_LOOPS (picoev.num_loops != 0) /* use after ++ */
#define PICOEV_FD_BELONGS_TO_LOOP(loop, fd) \
  ((loop)->loop_id == picoev.fds[fd].loop_id)
  
#define PICOEV_READ 1
#define PICOEV_WRITE 2
#define PICOEV_ACCEPT 4
  
  typedef unsigned picoev_loop_id_t;
  
  typedef struct picoev_loop_st picoev_loop;
  
  typedef void picoev_handler(picoev_loop* loop, int fd, int revents,
			      void* cb_arg);
  
  typedef struct picoev_fd_st {
    /* use accessors! */
    picoev_handler* callback;
    void* cb_arg;
    picoev_loop_id_t loop_id;
    short events;
  } picoev_fd;
  
  struct picoev_loop_st {
    /* read only */
    picoev_loop_id_t loop_id;
  };
  
  typedef struct picoev_globals_st {
    /* read only */
    picoev_fd* fds;
    int max_fd;
    int num_loops;
  } picoev_globals;
  
  extern picoev_globals picoev;
  
  /* initializes picoev */
  PICOEV_INLINE
  void picoev_init(int max_fd) {
    assert(! PICOEV_IS_INITED);
    assert(max_fd > 0);
    picoev.fds = (picoev_fd*)malloc(sizeof(picoev_fd) * max_fd);
    assert(PICOEV_NO_MEMORY(picoev.fds));
    picoev.max_fd = max_fd;
    picoev.num_loops = 0;
  }
  
  /* deinitializes picoev */
  PICOEV_INLINE
  void picoev_deinit(void) {
    assert(PICOEV_IS_INITED);
    free(picoev.fds);
    picoev.fds = NULL;
    picoev.max_fd = 0;
    picoev.num_loops = 0;
  }
  
  /* internal: updates events to be watched */
  void picoev_update_events_internal(picoev_loop* loop, int fd, int events);
  
  /* registers a file descriptor and callback argument to a event loop */
  PICOEV_INLINE
  void picoev_add(picoev_loop* loop, int fd, int events,
		  picoev_handler* callback, void* cb_arg) {
    picoev_fd* target;
    assert(PICOEV_IS_INITED_AND_FD_IN_RANGE(fd));
    target = picoev.fds + fd;
    assert(target->loop_id == 0);
    target->callback = callback;
    target->cb_arg = cb_arg;
    target->loop_id = loop->loop_id;
    picoev_update_events_internal(loop, fd, events);
  }
  
  /* unregisters a file descriptor from event loop */
  PICOEV_INLINE
  void picoev_del(picoev_loop* loop, int fd) {
    picoev_fd* target;
    assert(PICOEV_IS_INITED_AND_FD_IN_RANGE(fd));
    assert(PICOEV_FD_BELONGS_TO_LOOP(loop, fd));
    target = picoev.fds + fd;
    if (target->events != 0) {
      picoev_update_events_internal(loop, fd, 0);
    }
    target->loop_id = 0;
  }
  
  /* returns events being watched for given descriptor */
  PICOEV_INLINE
  int picoev_get_events(picoev_loop* loop __attribute__((unused)), int fd) {
    assert(PICOEV_IS_INITED_AND_FD_IN_RANGE(fd));
    assert(PICOEV_FD_BELONGS_TO_LOOP(loop, fd));
    return picoev.fds[fd].events;
  }
  
  /* sets events to be watched for given desriptor */
  PICOEV_INLINE
  void picoev_set_events(picoev_loop* loop, int fd, int events) {
    assert(PICOEV_IS_INITED_AND_FD_IN_RANGE(fd));
    assert(PICOEV_FD_BELONGS_TO_LOOP(loop, fd));
    if (picoev.fds[fd].events != events) {
      picoev_update_events_internal(loop, fd, events);
    }
  }
  
  /* creates a new event loop */
  PICOEV_INLINE
  picoev_loop* picoev_create_loop(void) {
    picoev_loop* loop;
    assert(PICOEV_IS_INITED);
    loop = (picoev_loop*)malloc(sizeof(picoev_loop));
    assert(PICOEV_NO_MEMORY(loop));
    loop->loop_id = ++picoev.num_loops;
    assert(PICOEV_TOO_MANY_LOOPS);
    return loop;
  }
  
  /* destroys a event loop */
  PICOEV_INLINE
  void picoev_destroy_loop(picoev_loop* loop) {
    free(loop);
  }
  
  /* loop once */
  void picoev_loop_once(picoev_loop* loop);
  
#undef PICOEV_INLINE

#ifdef __cplusplus
}
#endif

#endif
