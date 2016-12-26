/*
 * PLUGDAEMON. Copyright (c) 1997 Peter da Silva. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the program and author may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef __STDC__
#define P(a) a
#define P0() (void)
#define P1(a,b) (a b)
#define P2(a,b,c,d) (a b, c d)
#define P3(a,b,c,d,e,f) (a b, c d, e f)
#else
#define P(a) ()
#define P0() ()
#define P1(a,b) (b) a b;
#define P2(a,b,c,d) (b) a b; c d;
#define P3(a,b,c,d,e,f) (b) a b; c d; e f;
#endif

void parse_args P((int ac, char **av));
void bailout P((char *message));
void logclient P((struct in_addr peer, char* status));
void fill_sockaddr_in P((struct sockaddr_in *buffer, u_long addr, u_short port));
void daemonize P((void));
void init_signals P((void));
void plug P((int fd1, int fd2));
struct dtab *select_target P((int clifd));

