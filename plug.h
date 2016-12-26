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

/* Exit statuses */
#define S_NORMAL 0
#define S_UNKNOWN 1
#define S_CONNECT 2
#define S_EXCEPT 3
#define S_FATAL 4
#define S_SYNTAX 5

typedef struct {
	struct timeval timeval;
	struct timezone timezone;
	struct sockaddr_in peer, listen, client, proxy, target;
	off_t nread[2], nwrite[2];
} loginfo_t;

typedef struct dtab {
	struct dtab *next;
	char *destname;
	char *connect;
	struct sockaddr_in addr;
	int nclients;
	int status;
	time_t went_bad;
} dest_t;

typedef struct ptab {
	struct ptab *next;
	int pid;
	struct dtab *dest;
} proc_t;

typedef struct ctab {
	struct ctab *next;
	unsigned long addr;
	struct dtab *dest;
	int status;
	int bucket;
	time_t last_touched;
} client_t;

typedef struct rtab {
	struct rtab *next;
	u_long addr;
	u_long netmask;
} rule_t;

void bail_no_val(char *);
void bailout (char *message, int status);
void daemonize (void);
void delete_client (client_t *client, client_t *back_ptr);
void fill_sockaddr_in (struct sockaddr_in *buffer, u_long addr, u_short port);
char *sa2ascii(struct sockaddr_in *, char *);
void forget_pid (int pid);
void init_signals (void);
void logclient (struct in_addr peer, char* status);
struct ptab *lookup_pid (int pid);
void parse_args (int ac, char **av);
int plug (int fd1, int fd2, loginfo_t *lp);
void remember_pid (int pid, struct dtab *target);
struct dtab *select_target (int clifd, loginfo_t *lp);
void tag_dest_bad (int pid, int status);
#ifdef WAITER_ALTDEF
void waiter (int sig, void *scp);
#else
void waiter (int sig, SA_HANDLER_ARG2_T code, void *scp);
#endif
void inform_undertaker(int pid, int status);
void undertaker(void);
void burials(int);
void add_filter_rule(char *);
void logout(int, loginfo_t *);
char *https_chat(int, char *);
char *raw_readline(int, char *, int);
void write_pidfile(void);
void update_pidfile(void);
