/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 *  This copyrighted material is made available to anyone wishing to use,
 *  modify, copy, or redistribute it subject to the terms and conditions
 *  of the GNU General Public License v.2.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <limits.h>

#include "o2cb/o2cb_client_proto.h"

struct client_message {
	char *cm_command;
	int cm_argcount;
	char *cm_format;
};

#define BEGIN_MESSAGES(_list) struct client_message _list[] = {
#define END_MESSAGES(_list) }; \
	int _list##_len = sizeof(_list) / sizeof(_list[0]);
#define DEFINE_MESSAGE(_name, _argcount, _format) [CM_##_name] = {	\
	.cm_command = #_name, 				\
	.cm_argcount = _argcount,			\
	.cm_format = #_name " " _format,		\
},

BEGIN_MESSAGES(message_list)
DEFINE_MESSAGE(MOUNT, 5, "%s %s %s %s %s")
DEFINE_MESSAGE(MRESULT, 4, "%s %s %d %s")
DEFINE_MESSAGE(UNMOUNT, 3, "%s %s %s")
DEFINE_MESSAGE(STATUS, 2, "%d %s")
DEFINE_MESSAGE(LISTFS, 2, "%s %s")
DEFINE_MESSAGE(LISTMOUNTS, 2, "%s %s")
DEFINE_MESSAGE(LISTCLUSTERS, 0, "")
DEFINE_MESSAGE(ITEMCOUNT, 1, "%u")
DEFINE_MESSAGE(ITEM, 1, "%s")
DEFINE_MESSAGE(DUMP, 0, "")
END_MESSAGES(message_list)

const char *message_to_string(client_message message)
{
	return message_list[message].cm_command;
}

/* No short reads allowed */
static int full_read(int fd, void *buf, size_t count)
{
	size_t off = 0;
	ssize_t rc = 0;

	while (off < count) {
		rc = read(fd, buf + off, count - off);
		if (rc == 0)
			return -EPIPE;
		if (rc == -1) {
			rc = -errno;
			if (rc == -EINTR)
				continue;
			break;
		}
		off += rc;
		rc = 0;
	}
	return rc;
}

/* No short writes allowed */
static int full_write(int fd, void *buf, size_t count)
{
	size_t off = 0;
	ssize_t rc = 0;

	while (off < count) {
		rc = write(fd, buf + off, count - off);
		if (rc == 0)
			return -EPIPE;
		if (rc == -1) {
			rc = -errno;
			if (rc == -EINTR)
				continue;
			break;
		}
		off += rc;
		rc = 0;
	}
	return rc;
}

int send_message(int fd, client_message message, ...)
{
	int rc;
	size_t len;
	va_list args;
	char mbuf[OCFS2_CONTROLD_MAXLINE];

	memset(mbuf, 0, OCFS2_CONTROLD_MAXLINE);
	va_start(args, message);
	rc = vsnprintf(mbuf, OCFS2_CONTROLD_MAXLINE,
		       message_list[message].cm_format, args);
	va_end(args);

	/* Remove the trailing space from zero-argument messages */
	if (!message_list[message].cm_argcount) {
		len = strlen(mbuf);
		if (mbuf[len - 1] == ' ')
			mbuf[len - 1] = '\0';
	}

	if (rc >= OCFS2_CONTROLD_MAXLINE)
		rc = -E2BIG;
	else
		rc = full_write(fd, mbuf, OCFS2_CONTROLD_MAXLINE);

	return rc;
}

static char *get_args(char *buf, int *argc, char **argv, char sep, int want)
{
	char *p = buf, *rp = NULL;
	int i = 0;

	/* Skip the first word, which is the command */
	p = strchr(buf, sep);
	if (!p)
		goto out;
	p += 1;
	argv[0] = p;

	for (i = 1; i < OCFS2_CONTROLD_MAXARGS; i++) {
		p = strchr(p, sep);
		if (!p) {
			rp = p + 1;
			break;
		}

		if (want == i)
			break;

		*p = '\0';
		p += 1;
		argv[i] = p;
	}

out:
	if (argc)
		*argc = i;

	/* Terminate the list, the caller expects us to */
	argv[i] = NULL;

	/* we ended by hitting \0, return the point following that */
	if (!rp)
		rp = strchr(buf, '\0') + 1;

	return rp;
}

int receive_message_full(int fd, char *buf, client_message *message,
			 char **argv, char **rest)
{
	int i, rc, len, count;
	client_message msg;
	char *r;

	rc = full_read(fd, buf, OCFS2_CONTROLD_MAXLINE);
	if (rc)
		goto out;

	/* Safety first */
	buf[OCFS2_CONTROLD_MAXLINE - 1] = '\0';
	/* fprintf(stderr, "Got messsage \"%s\"\n", buf); */


	for (i = 0; i < message_list_len; i++) {
		len = strlen(message_list[i].cm_command);
		if (!strncmp(buf, message_list[i].cm_command, len) &&
		    ((buf[len] == ' ') || (buf[len] == '\0')))
			break;
	}
	if (i >= message_list_len) {
		rc = -EBADMSG;
		goto out;
	}
	msg = i;

	r = get_args(buf, &count, argv, ' ',
		     message_list[msg].cm_argcount);
	if (count != message_list[msg].cm_argcount) {
		rc = -EBADMSG;
	} else {
		/* for (i = 0; i < count; i++)
			fprintf(stderr, "Arg %d: \"%s\"\n", i, argv[i]); */
		if (message)
			*message = msg;
		if (rest)
			*rest = r;
	}

out:
	return rc;
}

int receive_message(int fd, char *buf, client_message *message, char **argv)
{
	return receive_message_full(fd, buf, message, argv, NULL);
}

static int parse_itemcount(char **args, unsigned int *count)
{
	int rc = 0;
	unsigned long n;
	char *ptr = NULL;

	n = strtoul(args[0], &ptr, 10);
	if (ptr && *ptr != '\0') {
		fprintf(stderr, "Invalid error code string: %s", args[0]);
		rc = -EINVAL;
	} else if ((n == LONG_MAX) || (n > UINT_MAX)) {
		fprintf(stderr, "Item count %lu out of range", n);
		rc = -ERANGE;
	} else {
		*count = n;
	}

	return rc;
}

int parse_status(char **args, int *error, char **error_msg)
{
	int rc = 0;
	long err;
	char *ptr = NULL;

	err = strtol(args[0], &ptr, 10);
	if (ptr && *ptr != '\0') {
		fprintf(stderr, "Invalid error code string: %s", args[0]);
		rc = -EINVAL;
	} else if ((err == LONG_MIN) || (err == LONG_MAX) ||
		   (err < INT_MIN) || (err > INT_MAX)) {
		fprintf(stderr, "Error code %ld out of range", err);
		rc = -ERANGE;
	} else {
		*error_msg = args[1];
		*error = err;
	}

	return rc;
}


/*
 * A list is sent as
 *
 * ITEMCOUNT <count>
 * ITEM <item>
 *   x <count>
 * STATUS 0 OK
 *
 * If there are errors in the middle, we'll get a STATUS error.
 */
int receive_list(int fd, char *buf, char ***ret_list)
{
	int rc, done = 0;
	int error;
	unsigned int count = 0, seen = 0;
	char *error_msg;
	client_message message;
	char **list = NULL;
	char *argv[OCFS2_CONTROLD_MAXARGS + 1];

	/*
	 * States are simple.  If list==NULL, we haven't gotten ITEMCOUNT
	 * yet.  If list!=NULL and seen<count, we haven't gotten all of
	 * our items yet.  Once list!=NULL and seen==count, we're ready for
	 * our STATUS.
	 */
	while (!done) {
		rc = receive_message(fd, buf, &message, argv);
		if (rc < 0)
			break;

		switch (message) {
			case CM_STATUS:
				rc = parse_status(argv, &error, &error_msg);
				if (rc) {
					fprintf(stderr, "Bad status message: %s\n",
						strerror(-rc));
				} else if (error) {
					rc = -error;
					fprintf(stderr,
						"Error %d from daemon: %s\n",
						error, error_msg);
				} else if (!list || (seen < count)) {
					rc = -EINVAL;
					fprintf(stderr,
						"Unexpected STATUS 0 from daemon\n");
				} else
					done = 1;  /* Got it */
				break;

			case CM_ITEMCOUNT:
				if (list) {
					rc = -EINVAL;
					fprintf(stderr,
						"Unexpected itemcount\n");
					break;
				}

				rc = parse_itemcount(argv, &count);
				if (rc) {
					fprintf(stderr, "Bad itemcount message: %s\n",
						strerror(-rc));
					break;
				}

				list = malloc(sizeof(char *) * (count + 1));
				if (!list)
					rc = -ENOMEM;
				else
					memset(list, 0,
					       sizeof(char *) * (count + 1));
				break;

			case CM_ITEM:
				if (!argv[0]) {
					rc = -EINVAL;
					fprintf(stderr,
						"Bad item message\n");
				} else if (!argv[0][0]) {
					rc = -EINVAL;
					fprintf(stderr,
						"Empty item message\n");
				} else if (seen >= count) {
					rc = -E2BIG;
					fprintf(stderr,
						"Too many items!\n");
				} else {
					list[seen] = strdup(argv[0]);
					if (!list[seen])
						rc = -ENOMEM;
					else
						seen++;
				}
				break;

		default:
				rc = -EINVAL;
				fprintf(stderr,
					"Unexpected message %s from daemon\n",
					message_to_string(message));
				break;
		}

		if (rc)
			done = 1;
	}

	if (!rc) {
		if (ret_list)
			*ret_list = list;
	} else if (list) {
		for (seen = 0; list[seen]; seen++)
			free(list[seen]);
		free(list);
	}
	return rc;
}

void free_received_list(char **list)
{
	int i;

	for (i = 0; list[i]; i++)
		free(list[i]);
	free(list);
}

int client_listen(const char *path)
{
	struct sockaddr_un addr;
	socklen_t addrlen;
	int rv, s;

	/* we listen for new client connections on socket s */

	s = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (s < 0) {
		/* log_error("socket error %d %d", s, errno); */
		return s;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_LOCAL;
	strcpy(&addr.sun_path[1], path);
	addrlen = sizeof(sa_family_t) + strlen(addr.sun_path+1) + 1;

	rv = bind(s, (struct sockaddr *) &addr, addrlen);
	if (rv < 0) {
		/* log_error("bind error %d %d", rv, errno); */
		close(s);
		return rv;
	}

	rv = listen(s, 5);
	if (rv < 0) {
		/* log_error("listen error %d %d", rv, errno); */
		close(s);
		return rv;
	}

	/* log_debug("listen %d", s); */

	return s;
}

int client_connect(const char *path)
{
	struct sockaddr_un sun;
	socklen_t addrlen;
	int rv, fd;

	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		fd = -errno;
		goto out;
	}

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strcpy(&sun.sun_path[1], path);
	addrlen = sizeof(sa_family_t) + strlen(sun.sun_path+1) + 1;

	rv = connect(fd, (struct sockaddr *) &sun, addrlen);
	if (rv < 0) {
		close(fd);
		fd = -errno;
	}
 out:
	return fd;
}
