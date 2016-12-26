/*
 * PLUGDAEMON. Copyright (c) 2012 Peter da Silva. All rights reserved.
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

#include "includes.h"
#include "config.h"
#include "plug.h"

int daemonized = 0;
char *prog = NULL;
char tag[64];

char *sourceaddr = NULL, *sourceport = NULL;
char *proxyaddr = NULL;
char *httpsaddr = NULL, *httpsport = NULL;
char *session_file = NULL;
int debug = 0;
int verbose = 0;
int background = 1;
int logging = 0;
int onlyone = 0;
int sessionmode = 0;
int keepalive=0;
long timeout = 3600; /* seconds */
long retrytime = 0; /* seconds */
char *pidfile = NULL, *delete_pidfile = NULL;

dest_t *dest_list;
proc_t *proc_list[HASH_SIZE];
client_t *client_list;

rule_t *filter_rules = 0;

int nproxies = 0;
int nclients = 0;
int nprocs = 0;

char *version = "plugdaemon V2.5.5 Copyright (c) 2012 Peter da Silva";

char *usage_string = "[-V] [-P pidfile] [-S sessionfile] [-snklfod[d]...]\n"
		     "\t[-p proxy-addr] [-i srcaddr] [-a accept_rule]...\n"
		     "\t[-h HTTPS-proxy[:port]] [-t timeout] [-r retry]\n"
		     "\nport destaddr[:destport]...";

int
main(int ac, char **av)
{
	int srvfd, one;
	struct sockaddr_in src_sockaddr, *prx_sockaddr;
	struct sockaddr_in *https_sockaddr;
	loginfo_t loginfo, *lp;
	int pid;
	dest_t *target;
	int saved_errno;

	one = 1;
	prx_sockaddr = 0;
	https_sockaddr = NULL;

	parse_args(ac, av);

	if(session_file && session_file[0] == '-' && session_file[1] == '\0')
		session_file = NULL;

	if(debug || (verbose && !session_file))
		background = 0;

	if (sourceaddr)
		sprintf(tag, "(%.16s %.8s)", sourceaddr, sourceport);
	else if(sourceport)
		sprintf(tag, "(%.8s)", sourceport);
	else
		bailout("Can't happen, source port is null!", S_FATAL);

	if(debug>1)
		fprintf(stderr, "%s: %s\n", prog, tag);

	/* arguments parsed, get sockets ready */

	if(debug>2) fprintf(stderr, "set up listening socket\n");
	if ((srvfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		bailout("server socket", S_FATAL);

	if(setsockopt(srvfd, SOL_SOCKET, SO_REUSEADDR, (char *)&one,
		sizeof one) < 0) {
		bailout("set server socket options", S_FATAL);
	}

	fill_sockaddr_in(&src_sockaddr,
		sourceaddr?inet_addr(sourceaddr):htonl(INADDR_ANY),
		htons(atoi(sourceport)));

	if(httpsaddr) {
		if(debug>2) fprintf(stderr, "set up https proxy socket\n");

		if ((httpsport = strchr(httpsaddr, ':')))
			*httpsport++ = 0;
		else
			httpsport = "80";

		if (!(https_sockaddr = malloc(sizeof *https_sockaddr))) {
			bailout("alloc memory for https proxy socket", S_FATAL);
		}
		fill_sockaddr_in(https_sockaddr,
				 inet_addr(httpsaddr),
				 htons(atoi(httpsport)));
	}

	if(proxyaddr) {
		if(debug>2) fprintf(stderr, "set up outgoing socket\n");
		if (!(prx_sockaddr = malloc(sizeof *prx_sockaddr))) {
			bailout("alloc memory for proxy socket", S_FATAL);
		}
		fill_sockaddr_in(prx_sockaddr, inet_addr(proxyaddr), 0);
	}

	if(debug>2) fprintf(stderr, "set up target socket(s)\n");
	for(target = dest_list; target; target=target->next) {
		char *destaddr, *destport;

		destaddr = target->destname;
		if((destport = strchr(destaddr, ':')))
			*destport++ = 0;
		else
			destport = sourceport;

		if(https_sockaddr) {
			target->addr = *https_sockaddr;
			target->connect = malloc(strlen(destaddr) + strlen(destport) + 2);
			if(!target->connect)
				bailout("malloc", S_FATAL);
			sprintf(target->connect, "%s:%s", destaddr, destport);
		} else {
			fill_sockaddr_in(&(target->addr),
				inet_addr(destaddr), htons(atoi(destport)));
			target->connect = NULL;
		}

		target->nclients = 0;
		target->status = S_NORMAL;
		target->went_bad = (time_t)0;
		target->destname = NULL; /* it's been trashed anyway */
	}

	if(debug>2) fprintf(stderr, "set up logging\n");
	if(verbose) {
		lp = &loginfo;
		memset(lp, 0, sizeof loginfo);
		lp->listen = src_sockaddr;
		if(prx_sockaddr)
			lp->proxy = *prx_sockaddr;
	} else
		lp = NULL;

	daemonize();

	prog = tag; /* for logging */

	init_signals();

	if(debug>2) fprintf(stderr, "bind server socket\n");
	/* One ring to rule them all */
	if(bind(srvfd, (struct sockaddr *)&src_sockaddr, sizeof src_sockaddr) < 0)
		bailout("server bind", S_FATAL);

	if(logging)
		syslog(LOG_NOTICE, "%.64s: Plugdaemon started.", tag);

	listen(srvfd, 5);

	if(debug>2) fprintf(stderr, "start main loop\n");
	/* wait for connections and service them */
	while(1) {
		int clifd, prxfd, status;
		socklen_t cli_len;
		struct sockaddr_in cli_sockaddr;

		if(debug>1)
		    fprintf(stderr, "%d listening for new connections.\n",
			(int) getpid());

		cli_len = sizeof cli_sockaddr;
		do {
			clifd = accept(srvfd, (struct sockaddr *)&cli_sockaddr, &cli_len);
			saved_errno = errno;
			/* If a child process died, we'll get an interrupted
			 * system call here, so call the undertaker every time
			 * through the loop.
			 */
			undertaker();
		} while (clifd < 0 && saved_errno == EINTR);
		if(clifd < 0)
			bailout("client accept", S_FATAL);
		if(lp)
			gettimeofday(&lp->timeval, &lp->timezone);

		if(!(target = select_target(clifd, lp))) {
			close(clifd);
			continue;
		}

		if(debug>1)
		    fprintf(stderr, "%d forwarding %d to %s.\n",
			(int) getpid(), ntohs(cli_sockaddr.sin_port),
			sa2ascii(&target->addr, NULL));

		/* spawn a child and send the parent back to try again */
		if((pid = fork()) == -1)
			bailout("client fork", S_FATAL);

		if(pid) {
			close(clifd);
			remember_pid(pid, target);
			update_pidfile();
			continue;
		}

		/* OK, I'm the child. First, forget about my parent's
		 * pidfile so I don't accidentally blow it away when I
		 * exit, and close the server port so if my parent dies
		 * it can be resurrected without killing me too...
		 */
		delete_pidfile = 0;
		close(srvfd);

		if ((prxfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
			bailout("proxy socket", S_FATAL);

		if (prx_sockaddr) {
			if(bind(prxfd, (struct sockaddr *)prx_sockaddr,
				sizeof *prx_sockaddr) < 0)
			{
				bailout("proxy bind", S_FATAL);
			}
		}

		if (connect(prxfd, (struct sockaddr *)&(target->addr),
			    sizeof (target->addr)) < 0)
			bailout("proxy connect", S_CONNECT);

		if(debug>1)
		    fprintf(stderr, "%d connected proxy to %s.\n",
			(int) getpid(), sa2ascii(&target->addr, NULL));

		if(keepalive) {
			if(setsockopt(clifd, SOL_SOCKET, SO_KEEPALIVE,
				      (char *)&one, sizeof one) < 0) {
				bailout("set client socket options", S_FATAL);
			}
			if(setsockopt(prxfd, SOL_SOCKET, SO_KEEPALIVE,
				      (char *)&one, sizeof one) < 0) {
				bailout("set proxy socket options", S_FATAL);
			}
		}

		if(target->connect) {
			char *errmsg = https_chat(prxfd, target->connect);
			if(errmsg) {
				bailout(errmsg, S_FATAL);
			}
		}
		status = plug(clifd, prxfd, lp);
		logout(status, lp);
		exit(status);
	}
}

char *sa2ascii(struct sockaddr_in *a, char *bufp)
{
	static char tempbuf[SA2ASCII_BUFSIZ];

	if(!bufp) bufp = tempbuf;

	if(a->sin_addr.s_addr == htonl(INADDR_ANY))
		sprintf(bufp, "%d", ntohs(a->sin_port));
	else if(a->sin_port == 0)
		sprintf(bufp, "%.71s", inet_ntoa(a->sin_addr));
	else
		sprintf(bufp, "%.71s:%d",
			inet_ntoa(a->sin_addr), ntohs(a->sin_port));

	return bufp;
}

#define JUMP_END(str,size,tmp) (tmp = strlen(str), size -= tmp, str += tmp)
#define ADD_BLANK(str,size) (*(str)++ = ' ', *(str) = 0, (size)++)

void
logout(int status, loginfo_t *lp)
{
	char logbuffer[BUFSIZ];
	char *buf_ptr = logbuffer;
	size_t buf_left = BUFSIZ, len;
	time_t t;
	struct timeval now;
	struct timezone zone;

	if(logging) {
	    if(status != S_NORMAL)
		syslog(LOG_NOTICE, "%.64s: Complete, status=%d.", tag, status);
	    else
		syslog(LOG_NOTICE, "%.64s: Complete.", tag);
	}

	if(!lp) return;

	gettimeofday(&now, &zone);

	t = time(NULL);
	strftime(buf_ptr, buf_left, "[%Y-%m-%d %H:%M:%S] ", localtime(&t));
	JUMP_END(buf_ptr,buf_left,len);

	sprintf(buf_ptr, "plug[%d] ", getpid());
	JUMP_END(buf_ptr,buf_left,len);

	sa2ascii(&lp->listen, buf_ptr);
	JUMP_END(buf_ptr,buf_left,len);
	ADD_BLANK(buf_ptr,buf_left);

	lp->peer.sin_port = 0; /* Don't care */
	sa2ascii(&lp->peer, buf_ptr);
	JUMP_END(buf_ptr,buf_left,len);
	ADD_BLANK(buf_ptr,buf_left);

	sa2ascii(&lp->target, buf_ptr);
	JUMP_END(buf_ptr,buf_left,len);
	ADD_BLANK(buf_ptr,buf_left);

	sprintf(buf_ptr, "%ld %ld ", (long)lp->nread[0], (long)lp->nread[1]);
	JUMP_END(buf_ptr,buf_left,len);

	now.tv_sec -= lp->timeval.tv_sec;
	now.tv_usec -= lp->timeval.tv_usec;
	if(now.tv_usec < 0) {
		now.tv_sec--;
		now.tv_usec += 1000000L;
	}
	if(now.tv_sec)
		sprintf(buf_ptr, "%ld%06ld\n",
			(long)now.tv_sec, (long)now.tv_usec);
	else
		sprintf(buf_ptr, "%ld\n", (long)now.tv_usec);
	JUMP_END(buf_ptr,buf_left,len);

	if(session_file) {
		int sfd = open(session_file, O_WRONLY|O_CREAT|O_APPEND, 0666);
		if(sfd>=0) {
			write(sfd, logbuffer, buf_ptr-logbuffer);
			close(sfd);
		}
	} else
		write(1, logbuffer, buf_ptr-logbuffer);
}

void
bail_no_val(char *optname)
{
	char msgbuf[1024];
	sprintf(msgbuf, "Missing value for %s", optname);
	bailout(msgbuf, S_SYNTAX);
}

void
bailout(char *message, int status)
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
		sprintf(p, "\nUsage: %.64s %s", prog, usage_string);
	}

	if(!daemonized)
		fprintf(stderr, "%s\n", msgbuf);
	else {
		syslog(LOG_ERR, "%s", msgbuf);
		closelog();
	}

	if(delete_pidfile)
		unlink(delete_pidfile);

	exit (status);
}

void
parse_args(int ac, char **av)
{
	dest_t *new_dest = NULL, *next_dest = NULL;

	if((prog = strrchr(*av, '/')))
		prog++;
	else
		prog = *av;

	while (*++av) {
		if (**av=='-') {
			while(*++*av) switch(**av) {
			    case 'i':
				if(!*++*av && !*++av)
					bail_no_val("interface (-i)");
				sourceaddr = *av;
				goto nextarg;
			    case 'p':
				if(!*++*av && !*++av)
					bail_no_val("proxy address (-p)");
				proxyaddr = *av;
				goto nextarg;
			    case 'P':
				if(!*++*av && !*++av)
					bail_no_val("PID file (-P)");
				pidfile = *av;
				goto nextarg;
			    case 'h':
				if(!*++*av && !*++av)
					bail_no_val("HTTP proxy (-h)");
				httpsaddr = *av;
				goto nextarg;
			    case 't':
				if(!*++*av && !*++av)
					bail_no_val("timeout (-t)");
				timeout = atol(*av);
				goto nextarg;
			    case 'r':
				if(!*++*av && !*++av)
					bail_no_val("retry time (-r)");
				retrytime = atol(*av);
				goto nextarg;
			    case 'a':
				if(!*++*av && !*++av)
					bail_no_val("access rule (-a)");
				add_filter_rule(*av);
				goto nextarg;
			    case 'S':
				if(!*++*av && !*++av)
					bail_no_val("session file (-S)");
				verbose++;
				session_file = *av;
				goto nextarg;
			    case 'k':
				keepalive++;
				continue;
			    case 'n':
				background = 0;
				continue;
			    case 'l':
				logging++;
				continue;
			    case 'd':
				debug++;
				continue;
			    case 'f':
				sessionmode++;
				continue;
			    case 'o':
				onlyone++;
				continue;
			    case 'V':
				printf("%s: %s\n", prog, version);
				exit(0); 
			    default:
				bailout("unknown argument", S_SYNTAX);
			}
		} else {
			if(!sourceport)
				sourceport = *av;
			else  {
				if(!httpsaddr) {
				    char *ptr;
				    for (ptr = *av; *ptr != '\0'; ptr++)
					if (strchr("0123456789:.", *ptr) == NULL)
					    bailout("target not in addr[:port] format", S_SYNTAX);
				}

				if(nproxies >= MAX_PROXIES)
					bailout("too many targets", S_SYNTAX);
				new_dest = malloc(sizeof (dest_t));
				if(!new_dest) {
					perror("malloc");
					bailout("Can't allocate target structure", S_FATAL);
				}

				new_dest->next = NULL;
				new_dest->destname = *av;

				if(!dest_list) { /* new list */
					dest_list = new_dest;
				} else { /* add to end of list */
					next_dest->next = new_dest;
				}
				next_dest = new_dest;

				nproxies++;
			}
		}
nextarg:	;
	}
	if(nproxies == 0)
		bailout("not enough arguments", S_SYNTAX);
	if(!sourceport)
		bailout("not enough arguments", S_SYNTAX);
}

#define NOSET ((fd_set *) NULL)
#define NOTIME ((struct timeval *) NULL)

int
plug(int fd1, int fd2, loginfo_t *lp)
{
	struct connx {
		int fd;			/* socket (bidirectional) */
		char buf[IO_SIZE];	/* Hold STUFF */
		int len, off;		/* tail, head pointers into buffer */
		int open;		/* socket still open for reading */
		int shutdown_wait;	/* other socket closed for reading,
					 * shut down writing when your buffer
					 * is done with
					 */
	} s[2];
	
	fd_set rset, wset, except_set;
	fd_set *p_eset;
	int nfds, nwr, nrd;
	int i;

	if(keepalive) p_eset = &except_set;
	else p_eset = NULL;

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
		if(p_eset) FD_ZERO(p_eset);

		for(i = 0; i < 2; i++) {
			if(s[i].open) {
				if(p_eset) {
					FD_SET(s[i].fd, p_eset);
				}
				if(s[i].len < IO_SIZE) {
					FD_SET(s[i].fd, &rset);
				}
				if(s[i].len > s[i].off) {
					FD_SET(s[!i].fd, &wset);
				}
			}
		}

		if(select(nfds, &rset, &wset, p_eset, NOTIME) < 0)
			bailout("proxy select", S_FATAL);

		for(i = 0; i < 2; i++) {
			if(p_eset && FD_ISSET(s[i].fd, p_eset)) {
				return S_EXCEPT; /* S_NORMAL? */
			}
			if(FD_ISSET(s[i].fd, &rset)) {
				nrd = read(s[i].fd, s[i].buf+s[i].len, IO_SIZE-s[i].len);
				if(nrd == -1) { /* Shouldn't happen */
					nrd = 0; /* fake EOF */
				}
				if(nrd == 0) {
					shutdown(s[i].fd, 0);
					if(s[i].len > s[i].off)
						s[!i].shutdown_wait = 1;
					else
						shutdown(s[!i].fd, 1);
					s[i].open = 0;
				}
				s[i].len += nrd;
				if(lp) lp->nread[i] += nrd;
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
						bailout("proxy write", S_EXCEPT);
				}
				s[i].off += nwr;
				if(s[i].off == s[i].len) {
					s[i].off = s[i].len = 0;
					if(s[!i].shutdown_wait) {
						shutdown(s[!i].fd, 1);
						s[!i].shutdown_wait = 0;
					}
				}
				if(lp) lp->nwrite[i] += nwr;
			}
		}
	}
	if(debug>1) fprintf(stderr, "%d completed.\n", (int) getpid());
	return S_NORMAL;
}

void
logclient(struct in_addr peer, char *status)
{
	char *s;

	s = inet_ntoa(peer);

	syslog(LOG_NOTICE, "%.64s: Connect from %.64s %s", tag, s, status);
}

void
logerror(char *event)
{
	char *msg = strerror(errno);

	if(!daemonized)
		fprintf(stderr, "%.64s: %s", event, msg);

	syslog(LOG_ERR, "%.64s: %.64s: %s", tag, event, msg);
}

void
fill_sockaddr_in(struct sockaddr_in *buffer, u_long addr, u_short port)
{
	memset(buffer, 0, sizeof *buffer);
	buffer->sin_family = AF_INET;
	buffer->sin_addr.s_addr = addr;
	buffer->sin_port = port;
}

int
check_peer(struct in_addr source)
{
	rule_t *p;
	u_long saddr;

	if(debug>1) {
		fprintf(stderr, "check_peer(%s)\n", inet_ntoa(source));
	}

	saddr = ntohl(source.s_addr);

	for(p = filter_rules; p; p=p->next) {
		if(debug>2) {
			fprintf(stderr, "Comparing %08lx&%08lx to %08lx\n",
				(long)saddr, (long)p->netmask, (long)p->addr);
		}
		if( (saddr & p->netmask) == p->addr ) {
			return 1;
		}
	}
	return 0;
}

void
add_filter_rule(char *network)
{
	rule_t *p;
	char *subnet;
	in_addr_t addr;
	u_short bits;

	subnet = strchr(network, '/');
	if(subnet) {
		*subnet++ = 0;
		bits = atoi(subnet);
	} else
		bits = 32;

	addr = inet_addr(network);

	p = malloc(sizeof *p);
	if(!p) bailout("malloc", S_FATAL);

	p->addr = ntohl(addr);
	p->netmask = -1 << (32-bits);

	p->next = filter_rules;
	filter_rules = p;
}

void
daemonize(void)
{
	int pid;

	if(background) {
		if((pid = fork()) == -1)
			bailout("daemon fork", S_FATAL);
		if(pid)
			exit(S_NORMAL);
	}

	write_pidfile();

	(void)openlog(prog, LOG_PID|LOG_CONS, LOG_DAEMON);

	if(background) {
		close(0);
		close(1);
		close(2);
		setsid();
		daemonized = 1;
	}
}

void
cleanup(int sig)
{
	if(delete_pidfile)
		unlink(delete_pidfile);
	exit(0);
}

/* OK, all waiter() does now is squirrel away the PIDs of the dying
 * binomes, so the undertaker can deliver them to the Principle Office
 * so their resources can be reused by later children. Doing this
 * keeps them from going viral and crashing the process table, I
 * suspect Megabyte is involved somewhere.
 *
 * The undertaker runs at a strategic spot in the main loop where it's
 * most likely that this bit of code will have been recently triggered.
 *
 * There's room for GRAVESITES-1 PIDs. If it ever gets to the point that
 * anything like that many processes are dying at once this code will
 * already be running into problems due to the undertaker itself getting
 * interrupted. The result of that will be a slow memory leak in plug.
 *
 * There's actually two arrays, and undertaker flips the index when it
 * starts work so new dead kids get deposited in the other one, this way
 * the waiter won't end up dropping a body on top of the undertaker.
 */
void
#ifdef WAITER_ALTDEF
waiter(int sig, void * scp)
#else
waiter(int sig, SA_HANDLER_ARG2_T code, void * scp)
#endif
{
	int status, pid;

	while (1) {
		pid = waitpid(-1, &status, WNOHANG);

		if(pid == 0 || pid == -1)
			break;

		if(debug>1)
			fprintf(stderr, "%d child %d died, status is %d.\n",
				(int) getpid(), pid, status);

		inform_undertaker(pid, status);
	}
}

void
init_signals(void)
{
	struct sigaction zombiesig, junksig;

	/* Wait for dead kids */
	zombiesig.SA_HANDLER = waiter;
	sigemptyset(&zombiesig.sa_mask);
	zombiesig.sa_flags = SA_NOCLDSTOP | SA_RESTART;

	if(sigaction(SIGCHLD, &zombiesig, &junksig) < 0)
		bailout("zombie signal", S_FATAL);

	signal(SIGTERM, cleanup);
}

void delete_client (client_t *client, client_t *back_ptr)
{
	if(client->dest)
		client->dest->nclients--;
	nclients--;

	if(back_ptr)
		back_ptr->next = client->next;
	else
		client_list = client->next;
	free(client);
}

struct dtab *select_target(int clifd, loginfo_t *lp)
{
	struct sockaddr_in p_addr;
	socklen_t len;
	client_t *client = 0;
	dest_t *target = NULL;
	static dest_t *dest_next = 0;
	time_t now;

	if(lp || sessionmode || logging || filter_rules) {
		len = sizeof p_addr;
		if(getpeername( clifd, (struct sockaddr *)&p_addr, &len) == -1) {
			/* Log instead of bailing because we're the parent. */
			logerror("getpeername");
			return 0;
		}
		if(lp)
			lp->peer = p_addr;
		if(filter_rules) {
			if(check_peer(p_addr.sin_addr) == 0) {
				logclient(p_addr.sin_addr, "Refused.");
				return 0;
			}
		}
	}

	now = time((time_t *)0);

	if(sessionmode) {
		client_t *back_ptr = 0;

		/* find a client and get rid of expired ones */
		client = client_list;
		while(client != NULL) {
			/* Clean up expired clients as we go */
			if(now - client->last_touched > timeout) {
				delete_client(client, back_ptr);
				client = back_ptr;
				if(!client)
					break;
			} else if(client->addr == p_addr.sin_addr.s_addr) {
				/* check to see if the dest is in failover */
			 	if(client->dest &&
				   client->dest->status != S_NORMAL) {
					delete_client(client, back_ptr);
					client = NULL;
				}
				break;
			}
			back_ptr = client;
			client = client->next;
		}

		if(client) { /* Old client, destination good. */
			target = client->dest;
		} else if(nclients < MAX_CLIENTS) {
			nclients++;

			client = malloc(sizeof (client_t));

			if(!client) {
				logerror("malloc");
				logclient(p_addr.sin_addr,
				    "aborted: out of memory, FATAL");
				bailout("Out of memory allocating client table", S_FATAL);
			}

			client->next = client_list;
			client_list = client;

			client->addr = p_addr.sin_addr.s_addr;
			client->status = 1;
			client->dest = (struct dtab *)0;
		} else {
			logclient(p_addr.sin_addr, "Too many clients.");
		}

		if(client)
			client->last_touched = now;
	}

	/* New client or we're not tracking sessions, find a target */
	if(!target) {
		int try;
		/* select a proxy. Dumb code to cycle them */
		if (onlyone) /* always start at head of list -- ADB */
			dest_next = NULL;
		for(try = 0; try < nproxies; try++) {
			if(dest_next)
				dest_next = dest_next->next;
			if(!dest_next)
				dest_next = dest_list;
			if(dest_next->status == S_NORMAL) {
				target = dest_next;
				break;
			}
			/* retry destinations periodically -- ADB */
			if(retrytime &&
			   now > dest_next->went_bad + retrytime) {
				if(debug>1) {
					fprintf(stderr,
					    "retrying destination %s.\n",
					    sa2ascii(&dest_next->addr, NULL));
				}
				target = dest_next;
				dest_next->status = S_NORMAL;
				break;
			}
		}
	}

	if(!target) { /* disaster! They're all bad! */
		struct dtab *dp;
		/* punt, mark them all good and pick the first.
		 * This is actually not a bad strategy if the client
		 * is a web browser, since they'll just get soft
		 * failures until one comes up.
		 */
		for(dp = dest_list; dp; dp = dp->next) {
			dp->status = S_NORMAL;
		}
		target = dest_next = dest_list;
		dest_next = dest_next->next;
	}

	if(client && !client->dest) {
		client->dest = target;
		target->nclients++;
	}

	if(logging) {
		char tmp[128]; /* big enough for IPv6, in ":" fmt */
		sprintf(tmp, "to %s", inet_ntoa(target->addr.sin_addr));
		logclient(p_addr.sin_addr, tmp);
	}

	if(lp) lp->target = target->addr;

	return target;
}

struct ptab *lookup_pid(int pid)
{
	proc_t *p;

	for(p = proc_list[hash(pid)]; p; p=p->next)
		if(p->pid == pid)
			break;

	return p;
}

void
remember_pid(int pid, struct dtab * target)
{
	proc_t *p = lookup_pid(pid);

	if(!p) {
		int bucket = hash(pid);
		if(nprocs>=MAX_CLIENTS*USAGE_FACTOR) {
			syslog(LOG_ERR,
				"%s: Client table full, pid %d", tag, pid);
			return;
		}
		if(!(p = malloc(sizeof (proc_t))))
			return;
		p->next = proc_list[bucket];
		proc_list[bucket] = p;
		nprocs++;
	}

	p->pid = pid;
	p->dest = target;
}

int graveyard = 0;
struct { int pid, status; } dead_children[2][GRAVESITES];
int next_dead_child[2] = { 0, 0 };

void inform_undertaker(int pid, int status) {
	int child;

	if(next_dead_child[graveyard] >= GRAVESITES) {
		bailout("Too many dead_children in inform_undertaker", S_FATAL);
	}
	child = next_dead_child[graveyard]++;

	dead_children[graveyard][child].pid = pid;
	dead_children[graveyard][child].status = status;
}

void undertaker(void)
{
	int funeral;

	for(funeral = 0; funeral < 2; funeral++) {
		graveyard = !graveyard;
		burials(!graveyard);
	}
}

void burials(where)
{
	int pid, status, child;

	if(next_dead_child[where] > 0) {
		while(next_dead_child[where] > 0) {
			child = --next_dead_child[where];
			pid = dead_children[where][child].pid;

			status = dead_children[where][child].status;
			status = WIFEXITED(status)
					? WEXITSTATUS(status)
					: S_FATAL;

			switch(status) {
				case S_CONNECT:
				case S_EXCEPT:
					tag_dest_bad(pid, status);
				case S_NORMAL:
				default:
					;
			}

			forget_pid(pid);
		}
	}
	update_pidfile();
}

void
forget_pid(int pid)
{
	proc_t *back_ptr = NULL;
	proc_t *p = proc_list[hash(pid)];

	while(p) {
		if(p->pid == pid)
			break;
		back_ptr = p;
		p = p->next;
	}

	if(!p)
		return;

	if(back_ptr)
		back_ptr->next = p->next;
	else
		proc_list[hash(pid)]=p->next;
	free(p);
}

void
tag_dest_bad(int pid, int status)
{
	proc_t *p = lookup_pid(pid);

	if(!p)
		return;

	/* OK, we know this destination has failed. Change its status */
	p->dest->status = status;
	p->dest->went_bad = time((time_t *)0);

	if(debug>1)
		fprintf(stderr, "destination %s bad status %d.\n",
			sa2ascii(&p->dest->addr, NULL), status);
}

void write_pidfile(void)
{
	FILE *fp;

	if(!pidfile) return;

	if (!(fp = fopen(pidfile, "w"))) {
		static char msgbuf[256]; /* text + 2 #s */
		sprintf(msgbuf, "PID file %.128s: Error %d", pidfile, errno);
		bailout(msgbuf, S_FATAL);
	}

	fprintf(fp, "%d\n", getpid());

	fclose(fp);

	delete_pidfile = pidfile;
}

void update_pidfile(void)
{
	FILE *fp;
	proc_t *p;
	int hindex;

	if(!pidfile) return;
	if(!delete_pidfile) return;

	if (!(fp = fopen(pidfile, "w")))
		return;

	fprintf(fp, "%d\n", getpid());

	for(hindex = 0; hindex < HASH_SIZE; hindex++) {
		for(p = proc_list[hindex]; p; p=p->next)
			fprintf(fp, "%d\n", p->pid);
	}

	fclose(fp);

	delete_pidfile = pidfile;
}

char *https_chat(int fd, char *name)
{
	static char chatbuf[IO_SIZE];
	char *errmsg;
	char *s;
	int result;

	if(debug>1)
		fprintf(stderr, "https_chat(%d, '%s');\n", fd, name);

	sprintf(chatbuf, "CONNECT %s HTTP/1.1\r\n\r\n", name);
	if(debug>1)
		fprintf(stderr, "%s\n", chatbuf);

	write(fd, chatbuf, strlen(chatbuf));

	if((errmsg = raw_readline(fd, chatbuf, IO_SIZE)))
		return errmsg;
	s = strchr(chatbuf, ' ');
	if(!s)
		goto synerr;
	if(!*++s)
		goto synerr;

	result = 0;
	while(isdigit(*s)) {
		result = result * 10 + *s - '0';
		s++;
	}
	if(result == 200) {
		while(1) {
			if((errmsg = raw_readline(fd, chatbuf, IO_SIZE)))
				return errmsg;
			for(s = chatbuf; isspace(*s); s++)
				continue;
			if(!*s)
				return NULL;
		}
	}
	while(isspace(*s))
		s++;
	if(*s)
		return s;
synerr:
	return "Malformed response from HTTPS proxy";
}

char *raw_readline(int fd, char *buffer, int maxlen)
{
	int n = 0;
	char *errmsg = NULL;
	char *s;
	char lastc = 0;

	if(debug > 1)
		fprintf(stderr, "raw_readline(%d, buffer, %d);\n", fd, maxlen);

	s = buffer;

	while(s - buffer < maxlen) {
		n = read(fd, s, 1);
		if(n<=0) {
			errmsg = n ? "Error on socket" : "Connection closed";
			break;
		}
		if(lastc == '\r') {
			if(*s == '\n') {
				*s = 0;
				if(debug > 1 && *buffer)
					fprintf(stderr, "%s\n", buffer);
				return 0;
			}
			/* slide a CR in under whatever the character is */
			*(s+1) = *s;
			*s = '\r';
			s++;
		}
		lastc = *s;
		if(*s != '\r')
			s++;
	}
	if(debug) {
		if(n) perror("read");
		*s = 0;
		if(*buffer)
			fprintf(stderr, "%s\n", buffer);
		if(!n)
			fprintf(stderr, "<EOF>\n");
	}
	return errmsg;
}
