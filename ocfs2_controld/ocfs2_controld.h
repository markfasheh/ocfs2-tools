/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 */

/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 *  This copyrighted material is made available to anyone wishing to use,
 *  modify, copy, or redistribute it subject to the terms and conditions
 *  of the GNU General Public License v.2.
 */

#ifndef __OCFS2_CONTROLD_H
#define __OCFS2_CONTROLD_H


#define DUMP_SIZE			(1024 * 1024)

extern char *prog_name;
extern int daemon_debug_opt;
extern char daemon_debug_buf[1024];
extern char dump_buf[DUMP_SIZE];
extern int dump_point;
extern int dump_wrap;

extern void daemon_dump_save(void);

#define log_debug(fmt, args...) \
do { \
	snprintf(daemon_debug_buf, 1023, "%ld %s@%d: " fmt "\n", \
		 time(NULL), __FUNCTION__, __LINE__, ##args); \
	if (daemon_debug_opt) fprintf(stderr, "%s", daemon_debug_buf); \
	daemon_dump_save(); \
} while (0)

#define log_group(g, fmt, args...) \
do { \
	snprintf(daemon_debug_buf, 1023, "%ld %s@%d %s " fmt "\n", \
		 time(NULL), __FUNCTION__, __LINE__, \
		 (g)->uuid, ##args); \
	if (daemon_debug_opt) fprintf(stderr, "%s", daemon_debug_buf); \
	daemon_dump_save(); \
} while (0)

#define log_error(fmt, args...) \
do { \
	log_debug(fmt, ##args); \
	syslog(LOG_ERR, fmt, ##args); \
} while (0)


#endif
