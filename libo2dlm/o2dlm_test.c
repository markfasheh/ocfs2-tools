/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Test driver for libo2dlm.
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 * Authors: Mark Fasheh <mark.fasheh@oracle.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <string.h>

#include "o2dlm.h"

#define COMMAND_MAX_LEN 4096

char cbuf[COMMAND_MAX_LEN];

#define DEFAULT_DLMFS_PATH "/dev/ocfs2/dlm/"

char *dlmfs_path = NULL;
char *prog;

struct o2dlm_ctxt *dlm_ctxt = NULL;

enum commands
{
	REGISTER = 0,
	UNREGISTER,
	LOCK,
	TRYLOCK,
	UNLOCK,
	GETLVB,
	SETLVB,
	HELP,
	NUM_COMMANDS
};

static char *command_strings[NUM_COMMANDS] = {
        [REGISTER]   "REGISTER",
        [UNREGISTER] "UNREGISTER",
        [LOCK]       "LOCK",
	[TRYLOCK]    "TRYLOCK",
	[UNLOCK]     "UNLOCK",
	[GETLVB]     "GETLVB",
	[SETLVB]     "SETLVB",
	[HELP]       "HELP",
};

#define NUM_PR_STRINGS 4
static char *pr_strings[NUM_PR_STRINGS] = {
	"PR",
	"PRMODE",
	"RO",
	"O2DLM_LEVEL_PRMODE"
};

#define NUM_EX_STRINGS 4
static char *ex_strings[NUM_EX_STRINGS] = {
	"EX",
	"EXMODE",
	"EX",
	"O2DLM_LEVEL_EXMODE"
};

struct command_s
{
	enum commands         c_type;
	char                  c_domain[O2DLM_DOMAIN_MAX_LEN];
	char                  c_id[O2DLM_LOCK_ID_MAX_LEN];
	enum o2dlm_lock_level c_level;
	char                  *c_lvb;
};

static void print_commands(void)
{
	printf("Domain Commands:\n");
	printf("register \"domain\'\n");
	printf("unregister \"domain\'\n");
	printf("Locking Commands - \"level\" is one of PR, or EX. "
	       "Some common variations are understood\n");
	printf("lock \"level\" \"lockid\"\n");
	printf("trylock \"level\" \"lockid\"\n");
	printf("unlock \"lockid\"\n");
	printf("getlvb \"lockid\"\n");
	printf("setlvb \"lockid\" \"lvb\"\n");
}

static int decode_type(struct command_s *c, char *buf)
{
	int i;

	for(i = 0; i < NUM_COMMANDS; i++) {
		if (!strcasecmp(buf, command_strings[i])) {
			c->c_type = i;
			return 0;
		}
	}
	return -1;
}

static void kill_return(char *buf)
{
	char *c;

	c = strchr(buf, '\n');
	if (c)
		*c = '\0';
}

static int decode_lock(struct command_s *c, char *buf)
{
	kill_return(buf);

	memset(c->c_id, 0, O2DLM_LOCK_ID_MAX_LEN);

	strncpy(c->c_id, buf, O2DLM_LOCK_ID_MAX_LEN - 1);

	return 0;
}

static int decode_domain(struct command_s *c, char *buf)
{
	kill_return(buf);

	memset(c->c_domain, 0, O2DLM_DOMAIN_MAX_LEN);

	strncpy(c->c_domain, buf, O2DLM_DOMAIN_MAX_LEN - 1);

	return 0;
}

static int decode_level(struct command_s *c, char *buf)
{
	int i;

	kill_return(buf);

	for (i = 0; i < NUM_PR_STRINGS; i++) {
		if (!strcasecmp(buf, pr_strings[i])) {
			c->c_level = O2DLM_LEVEL_PRMODE;
			return 0;
		}
	}

	for (i = 0; i < NUM_EX_STRINGS; i++) {
		if (!strcasecmp(buf, ex_strings[i])) {
			c->c_level = O2DLM_LEVEL_EXMODE;
			return 0;
		}
	}

	return -1;
}

static void print_command(struct command_s *c, const char *str)
{
	printf("Command: %s ", command_strings[c->c_type]);
	switch (c->c_type) {
	case REGISTER:
	case UNREGISTER:
		printf("\"%s\" %s\n", c->c_domain, str);
		break;
	case LOCK:
	case TRYLOCK:
		if (c->c_level == O2DLM_LEVEL_PRMODE)
			printf("O2DLM_LEVEL_PRMODE ");
		else
			printf("O2DLM_LEVEL_EXMODE ");
		/* fall through */
	case GETLVB:
	case UNLOCK:
		printf("\"%s\" %s\n", c->c_id, str);
		break;
	case SETLVB:
		printf("\"%s\" \"%s\" %s\n", c->c_id, c->c_lvb, str);
		break;
	case HELP:
		printf("%s\n", str);
		break;
	default:
		printf("wha?!?!?\n");
	}
}

static int get_command(struct command_s *command)
{
	char *next;

again:
	printf("command: ");

	if (!fgets(cbuf, COMMAND_MAX_LEN, stdin))
		return -1;

	next = strtok(cbuf, " \n");
	if (!next)
		goto again;

	if (decode_type(command, next)) {
		fprintf(stderr, "Invalid command type \"%s\"\n", next);
		goto again;
	}

	if (command->c_type == HELP)
		return 0;

	next = strtok(NULL, " ");
	if (!next) {
		fprintf(stderr, "invalid input!\n");
		goto again;
	}

	switch (command->c_type) {
	case REGISTER:
	case UNREGISTER:
		if (decode_domain(command, next)) {
			fprintf(stderr, "Invalid domain \"%s\"\n", next);
			goto again;
		}
		break;
	case LOCK:
	case TRYLOCK:
		if (decode_level(command, next)) {
			fprintf(stderr, "Invalid lock level \"%s\"\n", next);
			goto again;
		}

		next = strtok(NULL, " ");
		if (!next) {
			fprintf(stderr, "invalid input!\n");
			goto again;
		}

		/* fall through */
	case SETLVB:
	case GETLVB:
	case UNLOCK:
		if (decode_lock(command, next)) {
			fprintf(stderr, "Invalid lock \"%s\"\n", next);
			goto again;
		}

		if (command->c_type == SETLVB) {
			/* for setlvb we want to get a pointer to the
			 * start of the string to stuff */
			next = strtok(NULL, "\n");
			if (!next) {
				fprintf(stderr, "invalid input!\n");
				goto again;
			}

			kill_return(next);
			command->c_lvb = next;
		}
		break;
	default:
		fprintf(stderr, "whoa, can't parse this\n");
	}
	return 0;
}

#define LVB_LEN 64
static char lvb_buf[LVB_LEN];

static errcode_t exec_command(struct command_s *c)
{
	errcode_t ret = 0;
	unsigned int bytes, len;

	switch (c->c_type) {
	case REGISTER:
		ret = o2dlm_initialize(dlmfs_path, c->c_domain, &dlm_ctxt);
		break;
	case UNREGISTER:
		ret = o2dlm_destroy(dlm_ctxt);
		dlm_ctxt = NULL;
		break;
	case LOCK:
		ret = o2dlm_lock(dlm_ctxt, c->c_id, 0, c->c_level);
		break;
	case UNLOCK:
		ret = o2dlm_unlock(dlm_ctxt, c->c_id);
		break;
	case TRYLOCK:
		ret = o2dlm_lock(dlm_ctxt, c->c_id, O2DLM_TRYLOCK, c->c_level);
		break;
	case GETLVB:
		ret = o2dlm_read_lvb(dlm_ctxt, c->c_id, lvb_buf, LVB_LEN,
				     &bytes);
		if (!ret) {
			printf("%u bytes read. LVB begins on following "
			       "line and is terminated by a newline\n",
			       bytes);
			printf("%.*s\n", bytes, lvb_buf);
		}
		break;
	case SETLVB:
		len = strlen(c->c_lvb);
		ret = o2dlm_write_lvb(dlm_ctxt, c->c_id, c->c_lvb, len,
				      &bytes);
		if (!ret)
			printf("%u bytes written.\n", bytes);
		break;
	case HELP:
	default:
		print_commands();
	}

	return ret;
}

int main(int argc, char **argv)
{
	errcode_t error;
	struct command_s c;

	initialize_o2dl_error_table();

	prog = argv[0];

	if (argc < 2) {
		dlmfs_path = DEFAULT_DLMFS_PATH;
		printf("No fs path provided, using %s\n", dlmfs_path);
	} else {
		dlmfs_path = argv[1];
		printf("Using fs at %s\n", dlmfs_path);
	}

	printf("Type \"help\" to see a list of commands\n");

	while (!get_command(&c)) {
		error = exec_command(&c);

		if (error) {
			print_command(&c, "failed!");
			com_err(prog, error, "returned\n");
		} else
			print_command(&c, "succeeded!\n");
	}
	return 0;
}
