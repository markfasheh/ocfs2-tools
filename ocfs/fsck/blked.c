/*
 * blked.c
 *
 * ocfs file system block editor
 *
 * Copyright (C) 2003, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
 * Authors: Sunil Mushran
 */

#include "fsck.h"

#define MAX_EXTENTS	2048
#define OCFS_HBT_WAIT	10

int prn_len = 1;
int cnt_err = 0;
int cnt_wrn = 0;
int cnt_obj = 0;
bool int_err = false;
bool prn_err = false;

__u32 OcfsDebugCtxt = 0;
__u32 OcfsDebugLevel = 0;
__u32 debug_context = 0;
__u32 debug_level = 0;
__u32 debug_exclude = 0;

bool never_mounted = false;
__u32 fs_version = 0;

ocfs_global_ctxt OcfsGlobalCtxt;
ocfsck_context ctxt;
extern void version(char *progname);
void handle_signal(int sig);
int parse_blked_cmdline(int argc, char **argv);
int blked_initialize(char **buf);
bool verify_params(void);
int edit_structure(ocfs_disk_structure *s, char *buf, int idx, int *changed,
		   char *option);

char *usage_str = 
"usage: blked.ocfs [OPTIONS] device\n"
"	-n No hearbeat check\n"
"	-V Version";

/*
 * parse_blked_cmdline()
 *
 */
int parse_blked_cmdline(int argc, char **argv)
{
	int c;
	int ret = -1;
	char *p;

	ctxt.no_hb_chk = false;
	ctxt.write_changes = false;
	ctxt.verbose = false;

	if (argc < 2) {
		usage();
		goto bail;
	}

	while (1) {
		c = getopt(argc, argv, "nwvV?h:l:o:");

		if (c == -1)
			break;

		switch (c) {
		case 'o':
			p = strchr(optarg, '.');
			if (!p)
				ctxt.offset = atoll(optarg);
			else {
				*p = '\0';
				ctxt.offset  = ((__u64) strtoul(optarg, NULL, 0)) << 32;
				ctxt.offset |= strtoul(++p, NULL, 0);
			}
			break;
		case 'h':
			ctxt.offset |= ((__u64) strtoul(optarg, NULL, 0)) << 32;
			break;
		case 'l':
			ctxt.offset |= strtoul(optarg, NULL, 0);
			break;
		case 'n':
			ctxt.no_hb_chk = true;
			break;
		case 'w':
			ctxt.write_changes = true;
			break;
		case 'v':
			ctxt.verbose = true;
			break;
		case 'V':
			version(argv[0]);
			goto bail;
		default:
		case '?':
			usage();
			goto bail;
		}
	}

	if (ctxt.write_changes)
		ctxt.no_hb_chk = false;

	ret = 0;
bail:
	return ret;
}				/* parse_blked_cmdline */

/*
 * blked_initialize()
 *
 */
int blked_initialize(char **buf)
{
	int ret = -1;
	int fd;

	if (ctxt.write_changes)
		ctxt.flags = O_RDWR | O_LARGEFILE | O_SYNC;
	else
		ctxt.flags = O_RDONLY | O_LARGEFILE;

	if (bind_raw(ctxt.device, &ctxt.raw_minor, ctxt.raw_device, sizeof(ctxt.raw_device)))
		goto bail;

	if (ctxt.verbose)
		CLEAR_AND_PRINT("Bound %s to %s", ctxt.device, ctxt.raw_device);

	if ((fd = myopen(ctxt.raw_device, ctxt.flags)) == -1) {
		LOG_ERROR("Error opening %s.\n%s.", ctxt.raw_device,
			  strerror(errno));
		goto bail;
	} else
		ctxt.fd = fd;

	if ((ctxt.hdr = malloc_aligned(OCFS_SECTOR_SIZE)) == NULL) {
		LOG_INTERNAL();
		goto bail;
	}

	if ((*buf = malloc_aligned(OCFS_SECTOR_SIZE)) == NULL) {
		LOG_INTERNAL();
		goto bail;
	}


	/* Read the super block */
	if (read_one_sector(fd, (char *)ctxt.hdr, 0, 0) == -1) {
		LOG_INTERNAL();
		goto bail;
	}

	/* Get the device size */
	if (get_device_size(fd) == -1) {
		LOG_ERROR("unable to get the device size. exiting");
		goto bail;
	}

	ret = 0;

bail:
	return ret;
}				/* blked_initialize */

/*
 * verify_params()
 *
 */
bool verify_params(void)
{
	bool ret = false;

	if (ctxt.offset % OCFS_SECTOR_SIZE) {
		LOG_ERROR("invalid offset. exiting");
		goto bail;
	}

	ret = true;
bail:
	return ret;
}				/* verify_params */

/*
 * main()
 *
 */
int main(int argc, char **argv)
{
	int i, changed;
	char *buf = NULL;
	ocfs_disk_structure *s;
	ocfs_layout_t *l;
	__u64 blocknum;
	char option = '\0';

	memset(&ctxt, 0, sizeof(ctxt));
	init_global_context();

#define INSTALL_SIGNAL(sig)						\
	do {								\
		if (signal(sig, handle_signal) == SIG_ERR) {		\
		    fprintf(stderr, "Could not set " #sig "\n");	\
		    goto bail;						\
		}							\
	} while (0)

	INSTALL_SIGNAL(SIGTERM);
	INSTALL_SIGNAL(SIGINT);

	init_raw_cleanup_message();

	if (parse_blked_cmdline(argc, argv) == -1)
		goto bail;

	if (!verify_params)
		goto bail;

	if (optind >= argc) {
		usage();
		goto bail;
	}

	version(argv[0]);

	strncpy(ctxt.device, argv[optind], OCFS_MAX_FILENAME_LENGTH);

	if (blked_initialize(&buf) == -1) {
		goto bail;
	}

	/* Exit if not an OCFS volume */
	if (memcmp(ctxt.hdr->signature, OCFS_VOLUME_SIGNATURE,
		   strlen(OCFS_VOLUME_SIGNATURE))) {
		printf("%s: bad signature in super block\n", ctxt.device);
		goto bail;
	}
#if 0
	/* Exit if heartbeat detected */
	if (!ctxt.no_hb_chk) {
		if (!check_heart_beat(&ctxt.fd, OCFSCK_PUBLISH_OFF,
				      OCFS_SECTOR_SIZE))
			goto quiet_bail;
	}
#endif

	if (read_one_sector(ctxt.fd, buf, ctxt.offset, 0) == -1) {
		LOG_INTERNAL();
		goto bail;
	}

	blocknum = ctxt.offset/OCFS_SECTOR_SIZE;

	l = &(ocfs_header_layout[ocfs_header_layout_sz - 1]);
	if (blocknum < l->block + l->num_blocks) {
		for (i = 0; i < ocfs_header_layout_sz; ++i) {
			option = '\0';
			l = &(ocfs_header_layout[i]);
			if ((s = l->kind) == NULL || s->cls == NULL ||
			    s->read == NULL || s->write == NULL)
			       	continue;
			if (blocknum >= l->block &&
			    blocknum < l->block + l->num_blocks) {
				s->output(buf, 0, NULL, stdout);
				while (ctxt.write_changes) {
					changed = 0;
					if (edit_structure(s, buf, 0, &changed, &option) != -1)
						continue;

					if (!changed)
						break;

					if ((confirm_changes(ctxt.offset, s, buf, 0, NULL)) == -1)
						LOG_PRINT("Abort write");

					break;
				}
			}
		}
	} else {
		s = find_matching_struct(buf, 0);
		if (s) {
			s->output(buf, 0, NULL, stdout);
			while (ctxt.write_changes) {
				changed = 0;
				if (edit_structure(s, buf, 0, &changed, &option) != -1)
					continue;

				if (!changed)
					break;

				if ((confirm_changes(ctxt.offset, s, buf, 0, NULL)) == -1)
					LOG_PRINT("Abort write");

				break;
			}
		} else
			LOG_ERROR("unknown structure");
	}

bail:
	myclose(ctxt.fd);

	unbind_raw(ctxt.raw_minor);

	free_aligned(buf);
	free_aligned(ctxt.hdr);
	exit(0);
}				/* main */

/*
 * edit_structure()
 *
 */
int edit_structure(ocfs_disk_structure *s, char *buf, int idx, int *changed, char *option)
{
	int ret = 0;
	int fld;
	ocfs_class *cls;
	ocfs_class_member *m;
	GString *cur;
	GString *dflt;
	char *newval = NULL;
	char *bad;
	char *loc;

	if ((newval = malloc(USER_INPUT_MAX)) == NULL) {
		LOG_INTERNAL();
		goto bail;
	}

	*changed = 0;
	cls = s->cls;
	*option = '\0';

	while (1) {
		cur = dflt = NULL;
		bad = NULL;
	
		LOG_PRINT("choose a field to edit (1-%d) or 'q' to quit) : ", cls->num_members);
		if (fgets(newval, USER_INPUT_MAX, stdin) == NULL) {
			ret = -1;
			break;
		}

		if ((loc = rindex(newval, '\n')) != NULL)
			*loc = '\0';

	       	if (strcasecmp(newval, "q") == 0 || strcasecmp(newval, "quit") == 0) {
			*option = tolower(*newval);
			ret = -1;
			break;
		}

		fld = strtol(newval, &bad, 10);
		fld--;

		if (bad == newval || IS_INVALID_FIELD_NUM(cls, fld)) {
			ret = 0;
			LOG_ERROR("bad field number");
			break;
		}
	
		// show current value and default value	
		m = &(cls->members[fld]);
		if (m->to_string(&cur, buf, &(m->type))==-1) {
			ret = -1;
			LOG_ERROR("to_string failed");
			break;
		}

		if (s->defaults(buf, &dflt, idx, fld)==-1) {
			ret = -1;
			LOG_ERROR("defaults failed");
			break;
		}

		LOG_PRINT("%s : %s (default=%s)\n", m->name,
			  cur ? cur->str : "", dflt ? dflt->str : "");

		// get new value and validate it
		if (fgets(newval, USER_INPUT_MAX, stdin) == NULL) {
			ret = -1;
			break;
		}

		if ((loc = rindex(newval, '\n')) != NULL)
			*loc = '\0';

	       	if (strcasecmp(newval, "q")==0 || strcasecmp(newval, "quit")==0) {
			ret = -1;
			break;
		}

		if (strcmp(newval, "?")==0 || strcasecmp(newval, "help")==0) {
			char *help = m->helptext(&(m->type));
			printf("%s\n", help);
			free(help);
			ret = 0;
			break;
		}

		if ((ret = m->from_string(newval, buf, &(m->type))) == -1) {
			LOG_ERROR("bad entry");
			ret = -1;
			break;
		}

		(*changed)++;
		
		if (dflt) {
			g_string_free(dflt, true);
			dflt = NULL;
		}

		if (cur) {
			g_string_free(cur, true);
			cur = NULL;
		}
	}

	if (dflt)
		g_string_free(dflt, true);

	if (cur)
		g_string_free(cur, true);

bail:
	safefree(newval);

	return ret;
}				/* edit_structure */
