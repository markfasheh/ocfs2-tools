/*
 * Test driver for libo2dlm.
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
	HELP,
	NUM_COMMANDS
};

static char *command_strings[NUM_COMMANDS] = {
        [REGISTER]   "REGISTER",
        [UNREGISTER] "UNREGISTER",
        [LOCK]       "LOCK",
	[TRYLOCK]    "TRYLOCK",
	[UNLOCK]     "UNLOCK",
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

static void print_command(struct command_s *c)
{
	printf("Command: %s ", command_strings[c->c_type]);
	switch (c->c_type) {
	case REGISTER:
	case UNREGISTER:
		printf("\"%s\"\n", c->c_domain);
		break;
	case LOCK:
	case TRYLOCK:
		if (c->c_level == O2DLM_LEVEL_PRMODE)
			printf("O2DLM_LEVEL_PRMODE ");
		else
			printf("O2DLM_LEVEL_EXMODE ");
		/* fall through */
	case UNLOCK:
		printf("\"%s\"\n", c->c_id);
		break;
	case HELP:
		printf("\n");
		break;
	default:
		printf("wha?!?!?\n");
	}
}

static int get_command(struct command_s *command)
{
	char *next;

again:
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
	case UNLOCK:
		if (decode_lock(command, next)) {
			fprintf(stderr, "Invalid lock \"%s\"\n", next);
			goto again;
		}
		break;
	default:
		fprintf(stderr, "whoa, can't parse this\n");
	}
	return 0;
}

static void exec_command(struct command_s *c)
{
	errcode_t ret;

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
	case HELP:
		print_commands();
		return;
	default:
		return;
	}

	if (ret)
		com_err(prog, ret, "while executing command");
}

int main(int argc, char **argv)
{
	struct command_s c;

	prog = argv[0];

	if (argc < 2) {
		printf("please provide a path to an ocfs2_dlmfs mount\n");
		return 0;
	}

	initialize_o2dl_error_table();

	dlmfs_path = argv[1];
	printf("Using fs at %s\n", dlmfs_path);

	while (!get_command(&c)) {
		print_command(&c);
		exec_command(&c);
	}
	return 0;
}
