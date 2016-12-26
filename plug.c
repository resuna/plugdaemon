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

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>
#include <sys/errno.h>
#include <sys/wait.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include "plug.h"
#include "proto.h"

int daemonized = 0;
char *sourceaddr = NULL, *sourceport = NULL;
char *destaddr = NULL, *destport = NULL;
char *prog;
int debug = 0;
int log = 0;
int force = 0;
int keepalive = 0;
long timeout = 3600; /* seconds */
char tag[64];

struct dtab {
	char *dest;
	struct sockaddr_in addr;
	int nclients;
	int status;
	time_t last_touched;
};

struct ctab {
	unsigned long addr;
	struct dtab *dest;
	int status;
	time_t last_touched;
};

struct dtab dest_tab[MAX_PROXIES];
struct ctab client_tab[MAX_CLIENTS];
int dest_next;

int nproxies = 0;
int nclients = 0;

char *version = "plugdaemon V1.2.2 Copyright (c) 1997-1998 Peter da Silva";

int
main P2(int, ac, char, **av)
{
	int srvfd;
	struct sockaddr_in srv_addr;
	int pid;

	parse_args(ac, av);

	if (sourceaddr)
		sprintf(tag, "(%.16s %.8s)", sourceaddr, sourceport);
	else
		sprintf(tag, "(%.8s)", sourceport);

	if(debug>1)
		fprintf(stderr, "%s: %s\n", prog, tag);

	/* arguments parsed, get sockets ready */

	if ((srvfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		bailout("server socket");

	fill_sockaddr_in(&srv_addr,
		sourceaddr?inet_addr(sourceaddr):htonl(INADDR_ANY),
		htons(atoi(sourceport)));

	for(dest_next = 0; dest_next < nproxies; dest_next++) {
		char *destaddr, *portaddr;
		struct dtab *target;

		target = &dest_tab[dest_next];
		destaddr = target->dest;
		if((portaddr = strchr(destaddr, ':')))
			*portaddr++ = 0;
		else
			portaddr = sourceport;

		fill_sockaddr_in(&(target->addr),
			inet_addr(destaddr), htons(atoi(portaddr)));

		target->nclients = 0;
		target->status = 1;
		target->last_touched = (time_t)0;
		target->dest = NULL; /* it's been trashed anyway */
	}

	daemonize();

	prog = tag; /* for logging */

	init_signals();

	/* One ring to rule them all */
	if(bind(srvfd, (struct sockaddr *)&srv_addr, sizeof srv_addr) < 0)
		bailout("server bind");

	if(log)
		syslog(LOG_NOTICE, "%s: Plugdaemon started.", tag);

	listen(srvfd, 5);

	/* wait for connections and service them */
	while(1) {
		int clifd, prxfd, cli_len;
		struct sockaddr_in c_addr;
		struct dtab *target;

		if(debug>1)
		    fprintf(stderr, "%d listening for new connections.\n",
			(int) getpid());

		cli_len = sizeof c_addr;
		clifd = accept(srvfd, (struct sockaddr *)&c_addr, &cli_len);
		if(clifd < 0)
			bailout("client accept");

		if(!(target = select_target(clifd))) {
			close(clifd);
			continue;
		}

		if(debug>1)
		    fprintf(stderr, "%d client connecting to %d.\n",
			(int) getpid(), ntohs(c_addr.sin_port));

		/* spawn a child and send the parent back to try again */
		if((pid = fork()) == -1)
			bailout("client fork");

		if(pid) {
			close(clifd);
			continue;
		}

		/* ok, I'm the child. */
		if ((prxfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
			bailout("proxy socket");

		if (connect(prxfd, (struct sockaddr *)&(target->addr), sizeof (target->addr)) < 0)
			bailout("proxy connect");

		if(debug>1)
		    fprintf(stderr, "%d connected proxy to %s:%d.\n",
			(int) getpid(), inet_ntoa(target->addr.sin_addr),
			ntohs(target->addr.sin_port));

		if(keepalive) {
			int one = 1;

			if(setsockopt(clifd, SOL_SOCKET, SO_KEEPALIVE,
				      (char *)&one, sizeof one) < 0) {
				bailout("set client socket options");
			}
			if(setsockopt(prxfd, SOL_SOCKET, SO_KEEPALIVE,
				      (char *)&one, sizeof one) < 0) {
				bailout("set proxy socket options");
			}
		}

		plug(clifd, prxfd);

		exit(0);
	}
}

void
bailout P1(char *, message)
{
	int save_errno;
	char msgbuf[1024];
	char *p;

	save_errno = errno;

	sprintf(msgbuf, "%.64s: %.64s", prog, message);
	p = msgbuf + strlen(msgbuf);
	if(save_errno) {
		sprintf(p, ": %.64s", strerror(save_errno));
	} else {
		sprintf(p, "\nUsage is %.64s %s", prog,
			"[-V] | [-lfkd[d]...] [-i srcaddr] [-t seconds] port destaddr[:destport]...");
	}

	if(!daemonized)
		fprintf(stderr, "%s\n", msgbuf);
	else {
		syslog(LOG_ERR, msgbuf);
		closelog();
	}

	exit (1);
}

void
parse_args P2(int,  ac, char **, av)
{
	if((prog = strrchr(*av, '/')))
		prog++;
	else
		prog = *av;

	while (*++av) {
		if (**av=='-') {
			while(*++*av) switch(**av) {
			    case 'i':
				if(!*++*av && !*++av)
					bailout("no value for -i option");
				sourceaddr = *av;
				goto nextarg;
			    case 't':
				if(!*++*av && !*++av)
					bailout("no value for -t option");
				timeout = atol(*av);
				goto nextarg;
			    case 'k':
				keepalive++;
				continue;
			    case 'l':
				log++;
				continue;
			    case 'd':
				debug++;
				continue;
			    case 'f':
				force++;
				continue;
			    case 'V':
				printf("%s: %s\n", prog, version);
				exit(0);
			    default:
				bailout("unknown argument");
			}
		} else {
			if(!sourceport)
				sourceport = *av;
			else  {
				char	*ptr;
				
				for (ptr = *av; *ptr != '\0'; ptr++)
					if (strchr("0123456789:.", *ptr) == NULL)
						bailout("proxy host specification not in addr[:port] format");
				
				if(nproxies >= MAX_PROXIES)
					bailout("too many proxies");
				dest_tab[nproxies++].dest = *av;
			}
		}
nextarg:	;
	}
	if(nproxies == 0)
		bailout("not enough arguments");
	if(!sourceport)
		bailout("not enough arguments");
}

#define NOSET ((fd_set *) NULL)
#define NOTIME ((struct timeval *) NULL)

void
plug P2(int, fd1, int, fd2)
{
	struct connx {
		int fd;			/* socket (bidirectional) */
		char buf[MAX_MTU];	/* Hold STUFF */
		int len, off;		/* tail, head pointers into buffer */
		int open;		/* socket still open for reading */
		int shutdown_wait;	/* other socket closed for reading,
					 * shut down writing when your buffer
					 * is done with
					 */
	} s[2];
	
	fd_set rset, wset;
	int nfds, nwr, nrd;
	int i;

	if(fd1>fd2)
		nfds=fd1+1;
	else
		nfds=fd2+1;

	s[0].fd = fd1;
	s[1].fd = fd2;

	s[0].len = s[1].len = s[0].off = s[1].off = 0;
	s[0].open = s[1].open = 1;
	s[0].shutdown_wait = s[1].shutdown_wait = 0;

	while(s[0].open || s[1].open) {
		FD_ZERO(&rset);
		FD_ZERO(&wset);

		for(i = 0; i < 2; i++) {
			if(s[i].open) {
				if(s[i].len < MAX_MTU)
					FD_SET(s[i].fd, &rset);
				if(s[i].len > s[i].off)
					FD_SET(s[!i].fd, &wset);
			}
		}

		if(select(nfds, &rset, &wset, NOSET, NOTIME) < 0)
			bailout("proxy select");

		for(i = 0; i < 2; i++) {
			if(FD_ISSET(s[i].fd, &rset)) {
				nrd = read(s[i].fd, s[i].buf+s[i].len, MAX_MTU-s[i].len);
				if(nrd == 0 || nrd == -1) {
					shutdown(s[i].fd, 0);
					if(s[i].len > s[i].off)
						s[!i].shutdown_wait = 1;
					else
						shutdown(s[!i].fd, 1);
					s[i].open = 0;
				}
				s[i].len += nrd;
			}
			if(FD_ISSET(s[!i].fd, &wset)) {
				nwr = write(s[!i].fd, s[i].buf+s[i].off, s[i].len-s[i].off);
				if(nwr == 0) {
					shutdown(s[!i].fd, 1);
					shutdown(s[i].fd, 0);
					s[i].open = 0;
				} else if (nwr < 0) {
					if(errno == EAGAIN)
						nwr = 0;
					else
						bailout("proxy write");
				}
				s[i].off += nwr;
				if(s[i].off == s[i].len) {
					s[i].off = s[i].len = 0;
					if(s[!i].shutdown_wait) {
						shutdown(s[!i].fd, 1);
						s[!i].shutdown_wait = 0;
					}
				}
			}
		}
	}
	if(debug>1) fprintf(stderr, "%d completed.\n", (int) getpid());
}

void
logclient P2(struct in_addr, peer, char *, status)
{
	char *s;

	s = inet_ntoa(peer);

	syslog(LOG_NOTICE, "%.64s: Connect from %.64s %s", tag, s, status);
}

void
fill_sockaddr_in P3(struct sockaddr_in *, buffer, u_long, addr, u_short, port)
{
	memset(buffer, 0, sizeof *buffer);
	buffer->sin_family = AF_INET;
	buffer->sin_addr.s_addr = addr;
	buffer->sin_port = port;
}

void
daemonize P0()
{
	int pid;

	if(!debug) {
		if((pid = fork()) == -1)
			bailout("daemon fork");
		if(pid)
			exit(0);
	}

	(void)openlog(prog, LOG_PID|LOG_CONS, LOG_DAEMON);

	if(!debug) {
		close(0);
		close(1);
		close(2);
		setsid();
		daemonized = 1;
	}
}

void
waiter P3(int, sig, SA_HANDLER_ARG2_T, code, void *, scp)
{
	int status, pid;

	while (1) {
		pid = waitpid(-1, &status, WNOHANG);

		if(pid == 0 || pid == -1)
			break;

		if(debug>1)
			fprintf(stderr, "%d child %d died, status is %d.\n",
				(int) getpid(), pid, status);
	}
}

void
init_signals P0()
{
	struct sigaction zombiesig, junksig;

	/* Wait for dead kids */
	zombiesig.SA_HANDLER = waiter;
	sigemptyset(&zombiesig.sa_mask);
	zombiesig.sa_flags = SA_NOCLDSTOP | SA_RESTART;

	if(sigaction(SIGCHLD, &zombiesig, &junksig) < 0)
		bailout("zombie signal");
}

struct dtab *select_target P1(int, clifd)
{
	struct sockaddr_in p_addr;
	int len;
	struct ctab *client = NULL;
	struct dtab *target = NULL;
	time_t now;
	int i;

	if(force || log) {
		len = sizeof p_addr;
		if(getpeername( clifd, (struct sockaddr *)&p_addr, &len) == -1)
			bailout("getpeername");
	}

	if(force) {
		int old_client = 0;

		now = time((time_t *)0);

		/* find a client and get rid of expired ones */
		for(i = 0; i < nclients; i++) {
			if(client_tab[i].addr == p_addr.sin_addr.s_addr)
				break;
			if(client_tab[i].addr) {
				if(now - client_tab[i].last_touched > timeout) {
					if(client_tab[i].dest)
						client_tab[i].dest->nclients--;

					/* clear */
					client_tab[i].addr = 0;
					client_tab[i].dest = (struct dtab *)0;
					client_tab[i].status = 0;
					client_tab[i].last_touched = 0;
					old_client = i;
				}
			} else
				old_client = i;
		}

		if(i < nclients) {
			client = &client_tab[i];
			target = client->dest;
			if(now - client->last_touched > timeout) {
				client->dest->nclients--;
				client->dest = (struct dtab *)0;
			}
		} else {
			if(old_client) {
				client = &client_tab[old_client];
			} else {
				if(nclients >= MAX_CLIENTS) {
					logclient(p_addr.sin_addr,
					    "aborted: too many clients");
					return (struct dtab *)0;
				}
				client = &client_tab[nclients++];
			}
			client->addr = p_addr.sin_addr.s_addr;
			client->status = 1;
			client->dest = (struct dtab *)0;
		}

		client->last_touched = now;
	}

	if(!force || !client->dest) {
		/* select a proxy. dumb code to cycle them */
		if(dest_next >= nproxies)
			dest_next = 0;
		target = &dest_tab[dest_next++];
		if(force) {
			client->dest = target;
			target->nclients++;
		}
	}

	if(log) {
		char tmp[64]; /* big enough for IPv6, in ":" fmt */
		sprintf(tmp, "to %s", inet_ntoa(target->addr.sin_addr));
		logclient(p_addr.sin_addr, tmp);
	}

	return target;
}
