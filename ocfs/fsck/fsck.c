/*
 * fsck.c
 *
 * ocfs file system check utility
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
 * Authors: Kurt Hackel, Sunil Mushran
 */

#include "fsck.h"

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

__u32 fs_num = 0;

ocfs_global_ctxt OcfsGlobalCtxt;
ocfsck_context ctxt;
extern void version(char *progname);

int edit_structure(ocfs_disk_structure *s, char *buf, int idx, int *changed, char *option);

char *usage_str = 
"usage: fsck.ocfs [OPTIONS] device\n"
"	-n No hearbeat check\n"
"	-w Writeable\n"
"	-V Version\n"
"	-v Verbose\n"
"	-q Quiet";

/*
 * parse_fsck_cmdline()
 *
 */
int parse_fsck_cmdline(int argc, char **argv)
{
	int off;
	int c;
	int ret = -1;

	ctxt.write_changes = false;
	ctxt.no_hb_chk = false;
	ctxt.verbose = false;
	ctxt.modify_all = false;
	ctxt.quiet = false;
	ctxt.dev_is_file = false;

	if (argc < 2) {
		usage();
		goto bail;
	}

	while (1) {
		off = 0;
		c = getopt(argc, argv, "wnVvmqf?");

		if (c == -1)
			break;

		switch (c) {
		case 'w':
			ctxt.write_changes = true;
			break;
		case 'n':
			ctxt.no_hb_chk = true;
			break;
		case 'm':
			ctxt.modify_all = true;
			break;
		case 'v':
			ctxt.verbose = true;
			break;
		case 'q':
			ctxt.quiet = true;
			break;
		case 'f':
			ctxt.dev_is_file = true;
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

	if (ctxt.modify_all)
		ctxt.verbose = true;

	if (ctxt.write_changes)
		ctxt.no_hb_chk = false;

	if (ctxt.dev_is_file)
		ctxt.no_hb_chk = true;

	ret = 0;
bail:
	return ret;
}				/* parse_fsck_cmdline */

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
	
		printf("choose a field to edit (1-%d, 's' to step, "
		       "'n' for next, 'd' for dirnode or 'q' to quit) : ", cls->num_members);
		if (fgets(newval, USER_INPUT_MAX, stdin) == NULL) {
			ret = -1;
			break;
		}

		if ((loc = rindex(newval, '\n')) != NULL)
			*loc = '\0';

	       	if (strcasecmp(newval, "q") == 0 || strcasecmp(newval, "quit") == 0 ||
		    strcasecmp(newval, "n") == 0 || strcasecmp(newval, "next") == 0 ||
		    strcasecmp(newval, "s") == 0 || strcasecmp(newval, "step") == 0 ||
		    strcasecmp(newval, "d") == 0 || strcasecmp(newval, "dirnode") == 0)  {
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
	
		/* show current value and default value	*/
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

		printf("%s : %s (default=%s)\n", m->name, 
		       cur ? cur->str : "", 
		       dflt ? dflt->str : "");

		/* get new value and validate it */
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


/*
 * fsck_initialize()
 *
 */
int fsck_initialize(char **buf)
{
	int ret = -1;
	int fd;

	if (ctxt.write_changes)
		ctxt.flags = O_RDWR | O_LARGEFILE | O_SYNC;
	else
		ctxt.flags = O_RDONLY | O_LARGEFILE;

	if (!ctxt.dev_is_file) {
		if (bind_raw(ctxt.device, &ctxt.raw_minor, ctxt.raw_device, sizeof(ctxt.raw_device)))
			goto bail;
		if (ctxt.verbose)
			CLEAR_AND_PRINT("Bound %s to %s", ctxt.device, ctxt.raw_device);
	} else
		strncpy(ctxt.raw_device, ctxt.device, sizeof(ctxt.raw_device));

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

	if ((ctxt.vol_bm = malloc_aligned(VOL_BITMAP_BYTES)) == NULL) {
		LOG_INTERNAL();
		goto bail;
	} else
		memset(ctxt.vol_bm, 0, VOL_BITMAP_BYTES);

	if ((*buf = malloc_aligned(OCFS_SECTOR_SIZE)) == NULL) {
		LOG_INTERNAL();
		goto bail;
	}

	/* Seek to the first block */
	if (myseek64(fd, 0, SEEK_SET) == -1) {
		LOG_INTERNAL();
		goto bail;
	}

	/* Read the super block */
	if (myread(fd, (char *)ctxt.hdr, OCFS_SECTOR_SIZE) == -1) {
		LOG_INTERNAL();
		goto bail;
	}

	/* Get the device size */
	if (get_device_size(fd) == -1) {
		LOG_ERROR("unable to get the device size. exiting");
		goto bail;
	}

	ctxt.vol_bm_data = g_array_new(false, true, sizeof(bitmap_data));
	ctxt.dir_bm_data = g_array_new(false, true, sizeof(bitmap_data));
	ctxt.ext_bm_data = g_array_new(false, true, sizeof(bitmap_data));
	ctxt.filenames = g_array_new(false, true, sizeof(str_data));

	ret = 0;

bail:
	return ret;
}				/* fsck_initialize */


/*
 * main()
 *
 */
int main(int argc, char **argv)
{
	int i;
	int ret;
	__u64 off;
	char *buf = NULL;
	char option = '\0';
	ocfs_disk_structure *s;
	ocfs_layout_t *l;
	int j;
	GHashTable *bad = NULL;
	str_data *fn;

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

	if (parse_fsck_cmdline(argc, argv) == -1)
		goto quiet_bail;

	if (optind >= argc) {
		usage();
		goto quiet_bail;
	}

	version(argv[0]);

	strncpy(ctxt.device, argv[optind], OCFS_MAX_FILENAME_LENGTH);

	if (fsck_initialize(&buf) == -1) {
		goto quiet_bail;
	}

	/* Exit if not an OCFS volume */
	if (memcmp(ctxt.hdr->signature, OCFS_VOLUME_SIGNATURE,
		   strlen(OCFS_VOLUME_SIGNATURE))) {
		printf("%s: bad signature in super block\n", ctxt.device);
		goto quiet_bail;
	}

	/* Exit if heartbeat detected */
	if (!ctxt.no_hb_chk) {
		if (!check_heart_beat(&ctxt.fd, OCFSCK_PUBLISH_OFF,
				      OCFS_SECTOR_SIZE))
			goto quiet_bail;
	}

	/* Check ocfs volume header blocks */
	for (i = 0; i < ocfs_header_layout_sz; i++) {
		option = '\0';
		l = &(ocfs_header_layout[i]);
		if ((s = l->kind) == NULL || s->cls == NULL ||
		    s->read == NULL || s->write == NULL) {
			continue;
		}

		CLEAR_AND_PRINT("Checking %s...", l->name);

		ret = 0;
		for (j = 0; j < l->num_blocks; j++) {
			bad = NULL;
			off = BLOCKS2BYTES((l->block+j));

			ret = read_print_struct(s, buf, off, j, &bad);
			if (ret == -2)
				break;
			if (ret != -1 && !ctxt.modify_all)
				continue;
#if 0
			while (ctxt.write_changes) {
				changed = 0;
				if (edit_structure(s, buf, j, &changed, &option) != -1)
					continue;

				if (!changed)
					break;

				if ((ret = confirm_changes(off, s, buf, j, bad)) == -1)
					LOG_PRINT("Abort write");

				break;
			}
#endif

			if (bad)
				g_hash_table_destroy(bad);

			if (option == 's')
				continue;

			else if (option == 'n' || option == 'd' || option == 'q')
				break;
		}

		if (i == 0 && never_mounted == true) {
			LOG_PRINT("Volume has never been mounted on any node. Exiting");
			goto bail;
		}

		if (option == 'q' || option == 'd')
			break;

		if (ret < 0 && s == &diskhdr_t) {
			LOG_ERROR("Volume header bad. Exiting");
			goto bail;
		}
	}

	if (option == 'q')
		goto bail;

	CLEAR_AND_PRINT("Checking Directories and Files...");
	traverse_dir_nodes(ctxt.fd, ctxt.hdr->root_off, "/");

	CLEAR_AND_PRINT("Checking Global Bitmap...");
	if (check_global_bitmap(ctxt.fd) == -1)
		LOG_ERROR("Global bitmap check failed");

	CLEAR_AND_PRINT("Checking Extent Bitmap...");
	if (check_node_bitmaps(ctxt.fd, ctxt.ext_bm_data, ctxt.ext_bm,
			       ctxt.ext_bm_sz, "extent") == -1)
		LOG_ERROR("Extent bitmap check failed");

	CLEAR_AND_PRINT("Checking Directory Bitmap...");
	if (check_node_bitmaps(ctxt.fd, ctxt.dir_bm_data, ctxt.dir_bm,
			       ctxt.dir_bm_sz, "directory") == -1)
		LOG_ERROR("Directory bitmap check failed");

bail:
	if (!int_err) {
		if (cnt_err == 0)
			CLEAR_AND_PRINT("%s: clean, %d objects, %u/%llu "
					"blocks", ctxt.device, cnt_obj,
				       	ctxt.vol_bm_data->len,
				       	ctxt.hdr->num_clusters);
		else
			CLEAR_AND_PRINT("%s: %d errors, %d objects, %u/%llu "
					"blocks", ctxt.device, cnt_err, cnt_obj,
					ctxt.vol_bm_data->len,
					ctxt.hdr->num_clusters);
	}

quiet_bail:
	myclose(ctxt.fd);

	unbind_raw(ctxt.raw_minor);

	printf("\n");

	if (ctxt.vol_bm_data)
		g_array_free(ctxt.vol_bm_data, true);

	if (ctxt.dir_bm_data)
		g_array_free(ctxt.dir_bm_data, true);

	if (ctxt.ext_bm_data)
		g_array_free(ctxt.ext_bm_data, true);

	if (ctxt.filenames) {
		for (i = 0; i < ctxt.filenames->len; ++i) {
			fn = &(g_array_index(ctxt.filenames, str_data, i));
			safefree(fn->str);
		}
		g_array_free(ctxt.filenames, true);
	}

	for (i = 0; i < OCFS_MAXIMUM_NODES; ++i)
		free_aligned(ctxt.dir_bm[i]);

	for (i = 0; i < OCFS_MAXIMUM_NODES; ++i)
		free_aligned(ctxt.ext_bm[i]);

	free_aligned(buf);
	free_aligned(ctxt.hdr);
	free_aligned(ctxt.vol_bm);

	exit(0);
}				/* main */
