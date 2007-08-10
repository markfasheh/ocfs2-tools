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

#ifndef __OCFS2_CONTROLD_INTERNAL_H
#define __OCFS2_CONTROLD_INTERNAL_H

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <syslog.h>
#include <sched.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/errno.h>
#include <linux/netlink.h>

#include <sys/un.h>
#include "kernel-list.h"
#include "libgroup.h"

#define MAXARGS			16
#define MAXLINE			256
#define MAXNAME			255
#define MAX_CLIENTS		8
#define MAX_MSGLEN		2048
#define MAX_OPTIONS_LEN		1024
#define DUMP_SIZE		(1024 * 1024)

#define OCFS2_CONTROLD_SOCK_PATH	"ocfs2_controld_sock"

#ifndef TRUE
#define TRUE (1)
#endif
#ifndef FALSE
#define FALSE (0)
#endif

enum {
	DO_STOP = 1,
	DO_START,
	DO_FINISH,
	DO_TERMINATE,
	DO_SETID,
	DO_DELIVER,
};

extern char *prog_name;
extern char *clustername;
extern int our_nodeid;
extern group_handle_t gh;
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

#define ASSERT(x) \
{ \
	if (!(x)) { \
		fprintf(stderr, "\nAssertion failed on line %d of file %s\n\n" \
			"Assertion:  \"%s\"\n", \
			__LINE__, __FILE__, #x); \
        } \
}

struct mountpoint {
	struct list_head	list;
	char			mountpoint[PATH_MAX+1];
	int			client;
};

struct mountgroup {
	struct list_head	list;
	uint32_t		id;
	struct list_head	members;
	int			memb_count;
	struct list_head	mountpoints;

	char			uuid[MAXNAME+1];
	char			cluster[MAXNAME+1];
	char			type[5];
	char			options[MAX_OPTIONS_LEN+1];
	char			device[PATH_MAX+1];

	int			last_stop;
	int			last_start;
	int			last_finish;
	int			last_callback;
	int			start_event_nr;
	int			start_type;

	int			error;
	char			error_msg[128];
	int			mount_client;
	int			mount_client_fd;
	int			mount_client_notified;
	int			mount_client_delay;
	int                     group_leave_on_finish;
	int			remount_client;
	int			state;
	int			kernel_mount_error;
	int			kernel_mount_done;
	int			got_kernel_mount;

	int			spectator;
	int			readonly;
	int			rw;

	void			*start2_fn;
};

/* mg_member opts bit field */

enum {
	MEMB_OPT_RW		= 1,
	MEMB_OPT_RO		= 2,
	MEMB_OPT_SPECT		= 4,
	MEMB_OPT_RECOVER	= 8,
};

/* these need to match the kernel defines of the same name in
   linux/fs/gfs2/lm_interface.h */

#define LM_RD_GAVEUP 308
#define LM_RD_SUCCESS 309

/* mg_member state: local_recovery_status, recovery_status */

enum {
	RS_NEED_RECOVERY = 1,
	RS_SUCCESS,
	RS_GAVEUP,
	RS_NOFS,
	RS_READONLY,
};

struct mg_member {
	struct list_head	list;
	int			nodeid;
	char			name[NAME_MAX+1];

	int			spectator;
	int			readonly;
	int			rw;
	uint32_t		opts;

	int			gone_event;
	int			gone_type;
	int			finished;

	int			ms_kernel_mount_done;
	int			ms_kernel_mount_error;
};

enum {
	MSG_JOURNAL = 1,
	MSG_OPTIONS,
	MSG_REMOUNT,
	MSG_PLOCK,
	MSG_WITHDRAW,
	MSG_MOUNT_STATUS,
	MSG_RECOVERY_STATUS,
	MSG_RECOVERY_DONE,
};


int do_read(int fd, void *buf, size_t count);
int do_write(int fd, void *buf, size_t count);
struct mountgroup *find_mg(const char *uuid);
struct mountgroup *find_mg_id(uint32_t id);
void do_stop(struct mountgroup *mg);
void do_start(struct mountgroup *mg, int type, int member_count,
	      int *nodeids);
void do_finish(struct mountgroup *mg);
void do_terminate(struct mountgroup *mg);

int setup_cman(void);
int process_cman(void);
char *nodeid2name(int nodeid);
int setup_groupd(void);
int process_groupd(void);
void exit_cman(void);

int do_mount(int ci, int fd, const char *fstype, const char *uuid,
	     const char *cluster, const char *device,
	     const char *mountpoint, struct mountgroup **mg_ret);
int do_mount_result(struct mountgroup *mg, int ci, int another,
		    const char *fstype, const char *uuid,
		    const char *errcode, const char *mountpoint);
int do_unmount(int ci, int fd, const char *fstype, const char *uuid,
	       const char *mountpoint);
int do_remount(int ci, char *dir, char *mode);
void ping_kernel_mount(char *table);

int client_send(int ci, char *buf, int len);

void update_flow_control_status(void);

void dump_state(void);

#endif
