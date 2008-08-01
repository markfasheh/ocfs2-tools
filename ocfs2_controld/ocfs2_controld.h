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


struct cgroup;
struct ckpt_handle;

extern char *prog_name;
extern int daemon_debug_opt;
extern char daemon_debug_buf[1024];
extern char dump_buf[DUMP_SIZE];
extern int dump_point;
extern int dump_wrap;
extern int our_nodeid;

extern void daemon_dump_save(void);

#define log_debug(fmt, args...) \
do { \
	snprintf(daemon_debug_buf, 1023, "%ld %s@%d: " fmt "\n", \
		 time(NULL), __FUNCTION__, __LINE__, ##args); \
	if (daemon_debug_opt) fprintf(stderr, "%s", daemon_debug_buf); \
	daemon_dump_save(); \
} while (0)

#define log_error(fmt, args...) \
do { \
	log_debug(fmt, ##args); \
	syslog(LOG_ERR, fmt, ##args); \
} while (0)


/* main.c */
int connection_add(int fd, void (*work)(int ci), void (*dead)(int ci));
void connection_dead(int ci);
void shutdown_daemon(void);

/* ckpt.c */
int setup_ckpt(void);
void exit_ckpt(void);
int ckpt_open_global(int write);
void ckpt_close_global(void);
int ckpt_open_node(int nodeid, struct ckpt_handle **handle);
int ckpt_open_this_node(struct ckpt_handle **handle);
void ckpt_close(struct ckpt_handle *handle);
int ckpt_global_store(const char *section, const char *data, size_t data_len);
int ckpt_global_get(const char *section, char **data, size_t *data_len);
int ckpt_section_store(struct ckpt_handle *handle, const char *section,
		       const char *data, size_t data_len);
int ckpt_section_get(struct ckpt_handle *handle, const char *section,
		     char **data, size_t *data_len);

/* stack-specific interfaces (cman.c) */
int setup_stack(void);
char *nodeid2name(int nodeid);
int validate_cluster(const char *cluster);
int get_clustername(const char **cluster);
int kill_stack_node(int nodeid);
void exit_stack(void);

/* cpg.c */
int setup_cpg(void (*daemon_joined)(int first));
void exit_cpg(void);
void for_each_node(struct cgroup *cg,
		   void (*func)(int nodeid,
				void *user_data),
		   void *user_data);
int group_join(const char *name,
	       void (*set_cgroup)(struct cgroup *cg, void *user_data),
	       void (*node_down)(int nodeid, void *user_data),
	       void *user_data);
int group_leave(struct cgroup *cg);

/* dlmcontrol.c */
int setup_dlmcontrol(void);
void exit_dlmcontrol(void);

/* mount.c */
void init_mounts(void);
int have_mounts(void);
int start_mount(int ci, int fd, const char *uuid, const char *device,
	     const char *mountpoint);
int complete_mount(int ci, int fd, const char *uuid, const char *errcode,
		   const char *mountpoint);
int remove_mount(int ci, int fd, const char *uuid, const char *mountpoint);
void dead_mounter(int ci, int fd);
void bail_on_mounts(void);
int send_mountgroups(int ci, int fd);

#endif
