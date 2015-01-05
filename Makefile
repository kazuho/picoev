###############################################################################
#
# Example 
#
# make -n CC=gcc LINUX_BUILD=1 CC_RELEASE_FLAGS=-O2 CC_DEBUG_FLAGS=-g
#
###############################################################################

ifdef LINUX_BUILD
PICOEV_SOURCE = picoev_epoll.c
endif
ifdef DARWING_BUILD
PICOEV_SOURCE = picoev_kqueue.c
endif
ifdef GENERIC_BUILD
PICOEV_SOURCE = picoev_select.c
endif

LIB_A = libpicoev.a
LIB_SO = libpicoev.so
LIBS = $(LIB_A) $(LIB_SO)

all: $(LIBS)

$(LIB_SO):	picoev.h picoev_w32.h $(PICOEV_SOURCE)
	$(CC) $(CC_RELEASE_FLAGS) $(CC_DEBUG_FLAGS) -fPIC -c -o picoev_shared.o $(PICOEV_SOURCE) && \
	$(CC) $(LD_DEBUG_FLAGS) $(LD_RELEASE_FLAGS) -shared -Wl,-soname,libpicoev.so -o libpicoev.so picoev_shared.o

$(LIB_A): picoev.h picoev_w32.h $(PICOEV_SOURCE)
	$(CC) $(CC_RELEASE_FLAGS) $(CC_DEBUG_FLAGS) -c -o picoev_static.o $(PICOEV_SOURCE) && \
	ar cr libpicoev.a picoev_static.o && \
	ranlib libpicoev.a

.PHONY: clean

clean:
	@rm -rf *.o $(LIBS)

