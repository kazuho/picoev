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
#include <limits.h>
#include <stdlib.h>
#include <time.h>

#define PICOEV_IS_INITED (picoev.max_fd != 0)  
#define PICOEV_IS_INITED_AND_FD_IN_RANGE(fd) ((fd) < picoev.max_fd)
#define PICOEV_TOO_MANY_LOOPS (picoev.num_loops != 0) /* use after ++ */
#define PICOEV_FD_BELONGS_TO_LOOP(loop, fd) \
  ((loop)->loop_id == picoev.fds[fd].loop_id)

#define PICOEV_TIMEOUT_VEC_OF(loop, idx) \
  ((loop)->timeout.vec + (idx) * picoev.timeout_vec_size)
#define PICOEV_TIMEOUT_VEC_OF_VEC_OF(loop, idx) \
  ((loop)->timeout.vec_of_vec + (idx) * picoev.timeout_vec_of_vec_size)
#define PICOEV_RND_UP(v, d) (((v) + (d) - 1) / (d) * (d))

#define PICOEV_SIMD_BITS 128
#define PICOEV_TIMEOUT_VEC_SIZE 128
#define PICOEV_LONG_BITS (sizeof(long) * 8)

#define PICOEV_READ 1
#define PICOEV_WRITE 2
#define PICOEV_TIMEOUT 4
  
  typedef unsigned int picoev_loop_id_t;
  
  typedef struct picoev_loop_st picoev_loop;
  
  typedef void picoev_handler(picoev_loop* loop, int fd, int revents,
			      void* cb_arg);
  
  typedef struct picoev_fd_st {
    /* use accessors! */
    /* should keep the size to 16 bytes on 32-bit arch, 32 bytes on 64-bit */
    picoev_handler* callback;
    void* cb_arg;
    picoev_loop_id_t loop_id;
    short events;
    short timeout_idx; /* -1 if not used, otherwise index of timeout_vec */
  } picoev_fd;
  
  struct picoev_loop_st {
    /* read only */
    picoev_loop_id_t loop_id;
    struct {
      long* vec;
      long* vec_of_vec;
      size_t base_idx;
      time_t base_time;
      int resolution;
    } timeout;
    time_t now;
  };
  
  typedef struct picoev_globals_st {
    /* read only */
    picoev_fd* fds;
    int max_fd;
    int num_loops;
    size_t timeout_vec_size; /* # of elements in picoev_loop.timeout.vec[0] */
    size_t timeout_vec_of_vec_size; /* ... in timeout.vec_of_vec[0] */
  } picoev_globals;
  
  extern picoev_globals picoev;
  
  /* initializes picoev */
  PICOEV_INLINE
  int picoev_init(int max_fd) {
    assert(! PICOEV_IS_INITED);
    assert(max_fd > 0);
    if ((picoev.fds = (picoev_fd*)valloc(sizeof(picoev_fd) * max_fd)) == NULL) {
      return -1;
    }
    picoev.max_fd = max_fd;
    picoev.num_loops = 0;
    picoev.timeout_vec_size
      = PICOEV_RND_UP(picoev.max_fd, PICOEV_SIMD_BITS) / PICOEV_LONG_BITS;
    picoev.timeout_vec_of_vec_size
      = PICOEV_RND_UP(picoev.timeout_vec_size, PICOEV_SIMD_BITS)
      / PICOEV_LONG_BITS;
    return 0;
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
  
  /* creates a new event loop (defined by each backend) */
  picoev_loop* picoev_create_loop(int max_timeout);
  
  /* destroys a loop (defined by each backend) */
  int picoev_destroy_loop(picoev_loop* loop);
  
  /* internal: updates events to be watched (defined by each backend) */
  int picoev_update_events_internal(picoev_loop* loop, int fd, int events);
  
  /* internal: poll once and call the handlers (defined by each backend) */
  int picoev_poll_once_internal(picoev_loop* loop, int max_wait);
  
  /* updates timeout */
  PICOEV_INLINE
  void picoev_set_timeout(picoev_loop* loop, int fd, int secs) {
    picoev_fd* target;
    long* vec, * vec_of_vec;
    size_t vi = fd / PICOEV_LONG_BITS, delta;
    assert(PICOEV_IS_INITED_AND_FD_IN_RANGE(fd));
    assert(PICOEV_FD_BELONGS_TO_LOOP(loop, fd));
    target = picoev.fds + fd;
    /* clear timeout */
    if (target->timeout_idx != -1) {
      vec = PICOEV_TIMEOUT_VEC_OF(loop, target->timeout_idx);
      if ((vec[vi] &= ~((unsigned long)LONG_MIN >> (fd % PICOEV_LONG_BITS)))
	  == 0) {
	vec_of_vec = PICOEV_TIMEOUT_VEC_OF_VEC_OF(loop, target->timeout_idx);
	vec_of_vec[vi / PICOEV_LONG_BITS]
	  &= ~((unsigned long)LONG_MIN >> (vi % PICOEV_LONG_BITS));
      }
      target->timeout_idx = -1;
    }
    if (secs != 0) {
      delta = (loop->now + secs - loop->timeout.base_time)
	/ loop->timeout.resolution;
      if (delta >= PICOEV_TIMEOUT_VEC_SIZE) {
	delta = PICOEV_TIMEOUT_VEC_SIZE - 1;
      }
      target->timeout_idx =
	(loop->timeout.base_idx + delta) % PICOEV_TIMEOUT_VEC_SIZE;
      vec = PICOEV_TIMEOUT_VEC_OF(loop, target->timeout_idx);
      vec[vi] |= (unsigned long)LONG_MIN >> (fd % PICOEV_LONG_BITS);
      vec_of_vec = PICOEV_TIMEOUT_VEC_OF_VEC_OF(loop, target->timeout_idx);
      vec_of_vec[vi / PICOEV_LONG_BITS]
	|= (unsigned long)LONG_MIN >> (vi % PICOEV_LONG_BITS);
    }
  }
  
  /* registers a file descriptor and callback argument to a event loop */
  PICOEV_INLINE
  int picoev_add(picoev_loop* loop, int fd, int events, int timeout_in_secs,
		 picoev_handler* callback, void* cb_arg) {
    picoev_fd* target;
    assert(PICOEV_IS_INITED_AND_FD_IN_RANGE(fd));
    target = picoev.fds + fd;
    assert(target->loop_id == 0);
    target->callback = callback;
    target->cb_arg = cb_arg;
    target->loop_id = loop->loop_id;
    target->events = 0;
    target->timeout_idx = -1;
    if (picoev_update_events_internal(loop, fd, events) != 0) {
      target->loop_id = 0;
      return -1;
    }
    picoev_set_timeout(loop, fd, timeout_in_secs);
    return 0;
  }
  
  /* unregisters a file descriptor from event loop */
  PICOEV_INLINE
  int picoev_del(picoev_loop* loop, int fd) {
    picoev_fd* target;
    assert(PICOEV_IS_INITED_AND_FD_IN_RANGE(fd));
    target = picoev.fds + fd;
    if (target->events != 0) {
      if (picoev_update_events_internal(loop, fd, 0) != 0) {
	return -1;
      }
    }
    picoev_set_timeout(loop, fd, 0);
    target->loop_id = 0;
    return 0;
  }
  
  /* returns events being watched for given descriptor */
  PICOEV_INLINE
  int picoev_get_events(picoev_loop* loop __attribute__((unused)), int fd) {
    assert(PICOEV_IS_INITED_AND_FD_IN_RANGE(fd));
    return picoev.fds[fd].events;
  }
  
  /* sets events to be watched for given desriptor */
  PICOEV_INLINE
  int picoev_set_events(picoev_loop* loop, int fd, int events) {
    assert(PICOEV_IS_INITED_AND_FD_IN_RANGE(fd));
    if (picoev.fds[fd].events != events) {
      if (picoev_update_events_internal(loop, fd, events) != 0) {
	return -1;
      }
    }
    return 0;
  }
  
  /* internal function */
  PICOEV_INLINE
  int picoev_init_loop_internal(picoev_loop* loop, int max_timeout) {
    loop->loop_id = ++picoev.num_loops;
    assert(PICOEV_TOO_MANY_LOOPS);
    /* TODO uses valloc to align memory, for future optimisation using SIMD */
    if ((loop->timeout.vec
	 = (long*)valloc((picoev.timeout_vec_size
			  + picoev.timeout_vec_of_vec_size)
			 * sizeof(long) * PICOEV_TIMEOUT_VEC_SIZE))
	== NULL) {
      --picoev.num_loops;
      return -1;
    }
    loop->timeout.vec_of_vec
      = loop->timeout.vec + picoev.timeout_vec_size * PICOEV_TIMEOUT_VEC_SIZE;
    loop->timeout.base_idx = 0;
    loop->timeout.base_time = time(NULL);
    loop->timeout.resolution
      = PICOEV_RND_UP(max_timeout, PICOEV_TIMEOUT_VEC_SIZE)
      / PICOEV_TIMEOUT_VEC_SIZE;
    return 0;
  }
  
  /* internal function */
  PICOEV_INLINE
  void picoev_deinit_loop_internal(picoev_loop* loop) {
    free(loop->timeout.vec);
  }
  
  /* internal function */
  PICOEV_INLINE
  void picoev_handle_timeout_internal(picoev_loop* loop) {
    size_t i, j, k;
    loop->now = time(NULL);
    for (;
	 loop->timeout.base_time <= loop->now - loop->timeout.resolution; 
	 loop->timeout.base_idx
	   = (loop->timeout.base_idx + 1) % PICOEV_TIMEOUT_VEC_SIZE,
	   loop->timeout.base_time += loop->timeout.resolution) {
      /* TODO use SIMD instructions */
      long* vec = PICOEV_TIMEOUT_VEC_OF(loop, loop->timeout.base_idx);
      long* vec_of_vec
	= PICOEV_TIMEOUT_VEC_OF_VEC_OF(loop, loop->timeout.base_idx);
      for (i = 0; i < picoev.timeout_vec_of_vec_size; ++i) {
	long vv = vec_of_vec[i];
	if (vv != 0) {
	  for (j = i * PICOEV_LONG_BITS; vv != 0; j++, vv <<= 1) {
	    if (vv < 0) {
	      long v = vec[j];
	      assert(v != 0);
	      for (k = j * PICOEV_LONG_BITS; v != 0; k++, v <<= 1) {
		if (v < 0) {
		  picoev_fd* fd = picoev.fds + k;
		  assert(fd->loop_id == loop->loop_id);
		  (*fd->callback)(loop, k, PICOEV_TIMEOUT, fd->cb_arg);
		}
	      }
	      vec[j] = 0;
	    }
	  }
	  vec_of_vec[i] = 0;
	}
      }
    }
  }
  
  /* loop once */
  PICOEV_INLINE
  int picoev_loop_once(picoev_loop* loop, int max_wait) {
    loop->now = time(NULL);
    if (picoev_poll_once_internal(loop, max_wait) != 0) {
      return -1;
    }
    picoev_handle_timeout_internal(loop);
    return 0;
  }
  
#undef PICOEV_INLINE

#ifdef __cplusplus
}
#endif

#endif
