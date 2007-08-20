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

#ifndef __O2CB_CONTROLD_H__
#define __O2CB_CONTROLD_H__

#include <sys/types.h>
#include <asm/types.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#include <netdb.h>
#include <limits.h>
#include <unistd.h>
#include <time.h>
#include <syslog.h>
#include <sched.h>
#include <signal.h>
#include <linux/netlink.h>


#define MAXARGS		8
#define MAXLINE		256
#define MAXCON		4
#define MAXNAME		255
#define MAX_NODES	256 /* should be same as MAX_GROUP_MEMBERS */

extern char *prog_name;
extern int daemon_debug_opt;
extern char daemon_debug_buf[256];

#define log_debug(fmt, args...) \
do { \
	snprintf(daemon_debug_buf, 255, "%ld " fmt "\n", time(NULL), ##args); \
	if (daemon_debug_opt) fprintf(stderr, "%s", daemon_debug_buf); \
} while (0)

#define log_error(fmt, args...) \
do { \
	log_debug(fmt, ##args); \
	syslog(LOG_ERR, fmt, ##args); \
} while (0)


/* action.c */
int add_configfs_node(const char *name, int nodeid, char *addr, int addrlen,
                      int local);
void del_configfs_node(const char *name);
void finalize_cluster(const char *name);
void initialize_o2cb(void);
void remove_stale_clusters(void);

/* member_xxx.c */
int setup_member(void);
int process_member(void);
char *nodeid2name(int nodeid);
char *get_cluster_name(void);

/* member_cman.c */
int is_cman_member(int nodeid);
void cman_statechange(void);

#endif

