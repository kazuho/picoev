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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include "picoev.h"
#include "picoev_w32.h"

#define EWOULDBLOCK WSAEWOULDBLOCK

#define HOST 0 /* 0x7f000001 for localhost */
#define PORT 23456
#define MAX_FDS 1024
#define TIMEOUT_SECS 10

static void setup_sock(int fd)
{
  char on = 1, r;
  unsigned long flag = 1;

  r = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
  assert(r == 0);
  ioctlsocket(fd, FIONBIO, &flag);
  assert(r == 0);
}

static void close_conn(picoev_loop* loop, int fd)
{
  picoev_del(loop, fd);
  close(picoev_w32_fd2sock(fd));
  printf("closed: %d\n", fd);
}

static void rw_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
  if ((events & PICOEV_TIMEOUT) != 0) {
    
    /* timeout */
    close_conn(loop, fd);
    
  } else if ((events & PICOEV_READ) != 0) {
    
    /* update timeout, and read */
    char buf[1024];
    ssize_t r;
    picoev_set_timeout(loop, fd, TIMEOUT_SECS);
    r = recv(picoev_w32_fd2sock(fd), buf, sizeof(buf), 0);
    switch (r) {
    case 0: /* connection closed by peer */
      close_conn(loop, fd);
      break;
    case -1: /* error */
      if (errno == EAGAIN || errno == EWOULDBLOCK) { /* try again later */
	break;
      } else { /* fatal error */
	close_conn(loop, fd);
      }
      break;
    default: /* got some data, send back */
      if (send(picoev_w32_fd2sock(fd), buf, r, 0) != r) {
	close_conn(loop, fd); /* failed to send all data at once, close */
      }
      break;
    }
  
  }
}

static void accept_callback(picoev_loop* loop, int fd, int events, void* cb_arg)
{
  int newfd = accept(picoev_w32_fd2sock(fd), NULL, NULL);
  if (newfd != -1) {
    int sock = picoev_w32_sock2fd(newfd);
    printf("connected: %d\n", sock);
    setup_sock(newfd);
    picoev_add(loop, sock, PICOEV_READ, TIMEOUT_SECS, rw_callback, NULL);
  }
}

int main(void)
{
  picoev_loop* loop;
  int listen_sock;
  char flag;

  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 0), &wsaData);

  /* listen to port */
  assert((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) != -1);
  flag = 1;
  assert(setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag))
	 == 0);
  struct sockaddr_in listen_addr;
  listen_addr.sin_family = AF_INET;
  listen_addr.sin_port = htons(PORT);
  listen_addr.sin_addr.s_addr = htonl(HOST);
  assert(bind(listen_sock, (struct sockaddr*)&listen_addr, sizeof(listen_addr))
	 == 0);
  assert(listen(listen_sock, 5) == 0);
  setup_sock(listen_sock);
  
  /* init picoev */
  picoev_init(MAX_FDS);
  /* create loop */
  loop = picoev_create_loop(60);
  /* add listen socket */
  picoev_add(loop, picoev_w32_sock2fd(listen_sock), PICOEV_READ, 0, accept_callback, NULL);
  /* loop */
  while (1) {
    fputc('.', stdout); fflush(stdout);
    picoev_loop_once(loop, 10);
  }
  /* cleanup */
  picoev_destroy_loop(loop);
  picoev_deinit();
  
  return 0;
}
