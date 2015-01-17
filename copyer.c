/*
 * copyer - meant to do copy of media files from Razuna Asset 
 * Management system to CasparCG server media path. Program
 * is meant to work based on CasparCG client command.
 *
 * Developed by Sysnova Informations Systems Ltd.
 * rakib.mullick@sysnova.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * To compile: gcc -Wall -o copyer copyer.c -lpthread
 */

#define	_GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <error.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <syslog.h>

#define	CMDLEN	256
#define PATHLEN	CMDLEN * 2

/* this structure contains list of files to be copied
 * and their state, whether they already copied or not
 */

struct ftable {
	char fname[CMDLEN], path[PATHLEN], state;
};

static struct ftable *filetable[CMDLEN];
static unsigned copyon, request;

struct sockaddr_in clientaddr;
static int sock;

static char *dest;

void logger(int prio, const char *fmt, ...)
{
	syslog(prio, "%s", fmt);
}

void init_filetable(void)
{
	int i;

	for (i = 0; i < CMDLEN; i++) {
		filetable[i] = malloc(sizeof(struct ftable));
		memset(filetable[i]->fname, 0, CMDLEN);
		memset(filetable[i]->path, 0, PATHLEN);
		filetable[i]->state = 0;	/* not copied */
	}
}

int getfilelen(const char *b)
{
	int len = 0;
	while (*b != ',') {
		b++;
		len++;
	}

	return len;
}

static void putfile(const char *b)
{
	int i = 0;
	b += 5;

	/* Find a empty slot */
	while (filetable[i]->state && i < CMDLEN)
		i++;

	if (i < CMDLEN) {
		int len = getfilelen(b);

		strncpy(filetable[i]->fname, b, len);
		b += len + 12;
		strncpy(filetable[i]->path, b, strlen(b));
		filetable[i]->state = 1;
		filetable[i]->fname[len] = '\0';
		printf("Inserting into slot=> %d\n", i);
		request++;
	}
}

void *thread_handler(void *arg)
{
	sigset_t *s = arg;
	int ret, sig, i;

	for(;;) {
		ret = sigwait(s, &sig);
		if (ret != 0) {
			fprintf(stderr,"sigwait error\n");
			continue;
		}

		if (sig == SIGUSR1) {
			// start copying
			for (i = 0; i < CMDLEN; i++) {

				if (filetable[i]->state == 1 && strlen(filetable[i]->fname) > 0) {
					int status, ret;
					char cmd[1024] = {0};

					snprintf(cmd, 1024, "cp %s%s %s", filetable[i]->path, filetable[i]->fname, dest);
					ret = system(cmd);
					if (ret == -1) {
						fprintf(stderr,"failed to copy\n");
						continue;
					}
					ret = waitpid(-1,&status,0);
					
					
					filetable[i]->state = 0;
					memset(filetable[i]->fname, 0, CMDLEN);
					memset(filetable[i]->path, 0, PATHLEN);
				}
			}
			copyon = 0;
		}
	}
}

void handleint(int sig)
{
	int i;
	for (i = 0; i < CMDLEN; i++) {
		if (filetable[i])
			free(filetable[i]);
	}

	closelog();
	exit(0);
}

int main(int argc, char *argv[])
{
	int ret, epollfd;

	struct epoll_event ev;
	pthread_t helperthread;
	pthread_attr_t attr;
	sigset_t set;

	struct sockaddr_in server_addr;

	openlog(NULL,LOG_NDELAY|LOG_NOWAIT|LOG_PID, LOG_DAEMON);

	if (argc != 3) {
		fprintf(stderr,"./programname -d dstloc\n");
		exit(1);
	} else {
		dest = argv[2];
		//dest = argv[4];
	}

	socklen_t clientlen = sizeof(clientaddr);
	
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		logger(LOG_ERR, "Failed to open socket\n");
		exit(1);
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(5060);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if(bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)))
	{
		logger(LOG_ERR, "Failed to bind %s", strerror(errno));
		exit(1);
	}

	epollfd = epoll_create1(0);
	if (epollfd < 0) {
		logger(LOG_ERR, "failed to create epoll=>%s\n", strerror(errno));
		exit(1);
	}

	ev.events = EPOLLIN;
	ev.data.fd = sock;

	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sock, &ev)) {
		logger(LOG_ERR, "epoll_ctl failed:%s\n", strerror(errno));
		exit(1);
	}

       ret = pthread_attr_init(&attr);
       if (ret != 0) {
		logger(LOG_ERR, "ptread_attr_init\n");
		exit(1);
	}

       ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
       if (ret != 0) {
		logger(LOG_ERR,"pthread_attr_setdetachstate failed\n");
		exit(1);
	}

	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);

	ret = pthread_sigmask(SIG_BLOCK, &set, NULL);	
	if (ret) {
		logger(LOG_ERR,"pthread_sigmask %s\n", strerror(errno));
		exit(1);
	}

	ret = pthread_create(&helperthread, &attr, thread_handler, &set);
	if (ret) {
		logger(LOG_ERR,"pthread_create failed\n");
		exit(1);
	}

	init_filetable();

	signal(SIGINT, handleint);
	signal(SIGKILL, handleint);
	daemon(0,0);

	for(;;) {
		char buf[CMDLEN];
		ret = epoll_wait(epollfd, &ev, 2, -1);
		if (ret == -1)
			continue;

		ret = recvfrom(sock, &buf, CMDLEN, MSG_DONTWAIT, &clientaddr, &clientlen);

		/* Get copy list */
		if (ret > 0) {
			buf[ret] = '\0';
			if (!strncmp("copy:", buf, 5)) {
				putfile(buf);
				pthread_kill(helperthread, SIGUSR1);
			}
		}
	}
}
