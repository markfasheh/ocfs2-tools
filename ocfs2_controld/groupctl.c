/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * groupctl.c - Poke groupd
 *
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 *  This copyrighted material is made available to anyone wishing to use,
 *  modify, copy, or redistribute it subject to the terms and conditions
 *  of the GNU General Public License v.2.
 */

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <assert.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "ocfs2_controld_internal.h"

#define OCFS2_CONTROLD_GROUP_NAME "ocfs2"
#define OCFS2_CONTROLD_GROUP_LEVEL 2  /* Because gfs_controld uses 2.
					 I think all that matters is that
					 we are a higher number than
					 fenced, which uses 0. */


/* save all the params from callback functions here because we can't
   do the processing within the callback function itself */

group_handle_t gh;
static int cb_action;
static char cb_name[MAX_GROUP_NAME_LEN+1];
static int cb_event_nr;
static unsigned int cb_id;
static int cb_type;
static int cb_member_count;
static int cb_members[MAX_GROUP_MEMBERS];
static int done;
static int tty_p;


static void stop_cbfn(group_handle_t h, void *private, char *name)
{
	cb_action = DO_STOP;
	strcpy(cb_name, name);
}

static void start_cbfn(group_handle_t h, void *private, char *name,
		       int event_nr, int type, int member_count, int *members)
{
	int i;

	cb_action = DO_START;
	strncpy(cb_name, name, MAX_GROUP_NAME_LEN);
	cb_event_nr = event_nr;
	cb_type = type;
	cb_member_count = member_count;

	for (i = 0; i < member_count; i++)
		cb_members[i] = members[i];
}

static void finish_cbfn(group_handle_t h, void *private, char *name,
			int event_nr)
{
	cb_action = DO_FINISH;
	strncpy(cb_name, name, MAX_GROUP_NAME_LEN);
	cb_event_nr = event_nr;
}

static void terminate_cbfn(group_handle_t h, void *private, char *name)
{
	cb_action = DO_TERMINATE;
	strncpy(cb_name, name, MAX_GROUP_NAME_LEN);
}

static void setid_cbfn(group_handle_t h, void *private, char *name,
		       unsigned int id)
{
	cb_action = DO_SETID;
	strncpy(cb_name, name, MAX_GROUP_NAME_LEN);
	cb_id = id;
}

static void deliver_cbfn(group_handle_t h, void *private, char *name,
			 int nodeid, int len, char *buf)
{
}

static group_callbacks_t callbacks = {
	stop_cbfn,
	start_cbfn,
	finish_cbfn,
	terminate_cbfn,
	setid_cbfn,
	deliver_cbfn
};

static char *str_members(void)
{
#define LINESIZE 1024
	static char buf[LINESIZE];
	int i, len = 0;

	memset(buf, 0, LINESIZE);

	for (i = 0; (i < cb_member_count) && (len < LINESIZE); i++)
		len += sprintf(buf+len, "%d ", cb_members[i]);
	return buf;
}

static void output(char *msg, ...)
{
	va_list args;

	if (tty_p)
		rl_crlf();

	va_start(args, msg);
	vfprintf(stdout, msg, args);
	va_end(args);

	if (tty_p)
		rl_on_new_line();
}

int process_groupd(void)
{
	int error = 0;

	error = group_dispatch(gh);
	if (error) {
		fprintf(stderr, "groupd_dispatch error %d errno %d\n", error, errno);
		goto out;
	}

	if (!cb_action)
		goto out;

	switch (cb_action) {
	case DO_STOP:
		output("stop %s\n", cb_name);
		break;

	case DO_START:
		output("start %s event %d type %d count %d members [%s]\n",
		       cb_name, cb_event_nr, cb_type, cb_member_count,
		       str_members());
		break;

	case DO_FINISH:
		output("finish %s\n", cb_name);
		break;

	case DO_TERMINATE:
		output("terminate %s\n", cb_name);
		break;

	case DO_SETID:
		output("set_id %s %x\n", cb_name, cb_id);
		break;

	default:
		fprintf(stderr, "Invalid cb_action: %d\n", cb_action);
		error = -EINVAL;
	}

 out:
	cb_action = 0;
	return error;
}

int setup_groupd(void)
{
	int rv;

	gh = group_init(NULL, OCFS2_CONTROLD_GROUP_NAME,
			OCFS2_CONTROLD_GROUP_LEVEL, &callbacks, 10);
	if (!gh) {
		fprintf(stderr, "group_init error %d\n", errno);
		return -ENOTCONN;
	}

	rv = group_get_fd(gh);
	if (rv < 0)
		fprintf(stderr, "group_get_fd error %d %d\n", rv, errno);

	return rv;
}

struct arg_list {
	struct list_head al_list;
	char *al_item;
};

static int add_item(struct list_head *list, const char *start,
		    const char *end)
{
	struct arg_list *al;
	size_t len = end - start;

	al = malloc(sizeof(struct arg_list));
	if (!al)
		return -ENOMEM;

	al->al_item = malloc(sizeof(char) * (len + 1));
	if (!al->al_item) {
		free(al);
		return -ENOMEM;
	}

	memcpy(al->al_item, start, len);
	al->al_item[len] = '\0';

	list_add_tail(&al->al_list, list);

	return 0;
}

static int split_args(const char *line, char ***args, int *argcount)
{
	size_t len;
	int i, count, started;
	int rc = 0;
	char *cur;
	char **list;
	struct list_head arglist = LIST_HEAD_INIT(arglist);
	struct list_head *p, *n;
	struct arg_list *al;
	assert(line);
	assert(args);

	cur = (char *)line;
	len = strlen(line);
	for (started = 0, count = 0, i = 0; i < len; i++) {
		if (strchr(" \t", line[i])) {
			if (started) {
				rc = add_item(&arglist, cur, line + i);
				if (rc)
					goto out;
				count++;
				started = 0;
			}
		} else {
			if (!started) {
				cur = (char *)line + i;
				started = 1;
			}
		}
	}

	if (started) {
		rc = add_item(&arglist, cur, line + i);
		if (rc)
			goto out;
		count++;
	}

	list = malloc(sizeof(char *) * (count + 1));
	if (!list) {
		rc = -ENOMEM;
		goto out;
	}

	i = 0;
	list_for_each(p, &arglist) {
		assert(i < count);

		al = list_entry(p, struct arg_list, al_list);
		list[i] = al->al_item;
		al->al_item = NULL;
		i++;
	}
	list[count] = NULL;
	*args = list;
	*argcount = count;

out:
	list_for_each_safe(p, n, &arglist) {
		al = list_entry(p, struct arg_list, al_list);
		if (al->al_item)
			free(al->al_item);
		list_del(&al->al_list);
		free(al);
	}

	return rc;
}

static int handle_join(char **args)
{
	int rc;

	rc = group_join(gh, args[1]);
	if (rc)
		fprintf(stderr, "group_join failed\n");

	return rc;
}

static int handle_leave(char **args)
{
	int rc;

	rc = group_leave(gh, args[1]);
	if (rc)
		fprintf(stderr, "group_leave failed\n");

	return rc;
}

static int handle_start_done(char **args)
{
	int rc;
	int event_nr;
	char *ptr = NULL;
	unsigned long nr;

	nr = strtoul(args[2], &ptr, 10);
	if (!ptr || *ptr || (nr > INT_MAX)) {
		fprintf(stderr, "Invalid event number: \"%s\"\n", args[2]);
		rc = -EINVAL;
		goto out;
	}
	event_nr = nr;

	rc = group_start_done(gh, args[1], event_nr);
	if (rc)
		fprintf(stderr, "group_leave failed\n");

out:
	return rc;
}

static int handle_stop_done(char **args)
{
	int rc;

	rc = group_stop_done(gh, args[1]);
	if (rc)
		fprintf(stderr, "group_leave failed\n");

	return rc;
}

struct command {
	char *c_cmd;
	int c_argcount;
	int (*c_handler)(char **args);
};

static struct command cmds[] = {
	{
		.c_cmd		= "join",
		.c_argcount	= 2,
		.c_handler	= handle_join,
	},
	{
		.c_cmd		= "leave",
		.c_argcount	= 2,
		.c_handler	= handle_leave,
	},
	{
		.c_cmd		= "start_done",
		.c_argcount	= 3,
		.c_handler	= handle_start_done,
	},
	{
		.c_cmd		= "stop_done",
		.c_argcount	= 2,
		.c_handler	= handle_stop_done,
	},
};
static int cmd_count = sizeof(cmds) / sizeof(cmds[0]);


static int handle_command(char *line)
{
	int i, rc, count = 0;
	struct command *cmd;
	char **args = NULL;

	rc = split_args(line, &args, &count);
	if (rc) {
		fprintf(stderr, "Unable to parse command \"%s\": %s\n",
			line, strerror(-rc));
		goto out;
	}

	for (cmd = NULL, i = 0; i < cmd_count; i++) {
		if (strcmp(args[0], cmds[i].c_cmd))
			continue;
		cmd = &cmds[i];
	}
	
	if (!cmd) {
		fprintf(stderr, "Invalid command: \"%s\"\n", args[0]);
		rc = -EINVAL;
		goto out;
	}

	if (count != cmd->c_argcount) {
		fprintf(stderr,
			"Incorrect number of arguments to \"%s\"\n",
			cmd->c_cmd);
		rc = -EINVAL;
		goto out;
	}

	rc = cmd->c_handler(args);

out:
	return rc;
}

static void _rl_callback(char *line)
{
	size_t len;

	if (!line) {
		if (tty_p)
			rl_crlf();
		rl_callback_handler_remove();
		done = 1;
		return;
	}

	if (!*line)
		return;

	len = strlen(line);
	if (line[len - 1] == '\n')
		line[len - 1] = '\0';

	add_history(line);

	fprintf(stdout, "Read the line \"%s\"\n", line);
	handle_command(line);
}

static int loop(int gfd)
{
	int rc = 0;
	int client_maxi, poll_timeout = -1;
	struct pollfd pollfds[2];

	tty_p = isatty(STDIN_FILENO);

	rl_callback_handler_install(tty_p ?  "groupctl> " : NULL,
				    _rl_callback);

	pollfds[0].fd = STDIN_FILENO;
	pollfds[0].events = POLLIN | POLLHUP;
	pollfds[1].fd = gfd;
	pollfds[1].events = POLLIN | POLLHUP;
	client_maxi = sizeof(pollfds) / sizeof(pollfds[0]);

	while (!done) {
		rc = poll(pollfds, client_maxi + 1, poll_timeout);
		if ((rc < 0) && (errno != EINTR))
			fprintf(stderr, "poll error %d errno %d\n", rc,
				errno);
		rc = 0;

		if (pollfds[1].revents & POLLIN) {
			process_groupd();
			rl_forced_update_display();
		}

		if (pollfds[1].revents & POLLHUP) {
			fprintf(stderr, "groupd connection died\n");
			done = 1;
			continue;
		}

		if (pollfds[0].revents & POLLIN)
			rl_callback_read_char();

		/*
		 * On HUP, make sure readline() got everything.
		 * The callback will make sure we clean up and exit
		 */
		if (pollfds[0].revents & POLLHUP)
			rl_callback_read_char();
	}

	return rc;
}

int main(int argc, char *argv[])
{
	int fd, rc;

	rc = setup_groupd();
	if (rc < 0)
		goto out;

	fd = rc;
	rc = loop(fd);

	group_exit(gh);

out:
	return rc;
}
