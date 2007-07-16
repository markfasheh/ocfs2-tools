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

#include <sys/types.h>
#include <asm/types.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
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
#include <dirent.h>

#include "o2cb_controld.h"
#include "ccs.h"

#if 0
static int dir_members[MAX_GROUP_MEMBERS];
static int dir_members_count;
#endif

static char cluster_dir[PATH_MAX] = "";
static char nodes_dir[PATH_MAX] = "";

#define CLUSTER_BASE     "/sys/kernel/config/cluster"
#define CLUSTER_FORMAT  CLUSTER_BASE "/%s"
#define NODES_FORMAT    CLUSTER_FORMAT "/nodes"


static int do_write(int fd, void *buf, size_t count)
{
	int rv, off = 0;

 retry:
	rv = write(fd, buf + off, count);
	if (rv == -1 && errno == EINTR)
		goto retry;
	if (rv < 0) {
		log_error("write errno %d", errno);
		return rv;
	}

	if (rv != count) {
		count -= rv;
		off += rv;
		goto retry;
	}
	return 0;
}

#if 0
static int do_sysfs(char *name, char *file, char *val)
{
	char fname[512];
	int rv, fd;

	sprintf(fname, "%s/%s/%s", DLM_SYSFS_DIR, name, file);

	fd = open(fname, O_WRONLY);
	if (fd < 0) {
		log_error("open \"%s\" error %d %d", fname, fd, errno);
		return -1;
	}

	log_debug("write \"%s\" to \"%s\"", val, fname);

	rv = do_write(fd, val, strlen(val) + 1);
	close(fd);
	return rv;
}

int set_control(char *name, int val)
{
	char buf[32];

	memset(buf, 0, sizeof(buf));
	snprintf(buf, 32, "%d", val);

	return do_sysfs(name, "control", buf);
}

int set_event_done(char *name, int val)
{
	char buf[32];

	memset(buf, 0, sizeof(buf));
	snprintf(buf, 32, "%d", val);

	return do_sysfs(name, "event_done", buf);
}

int set_id(char *name, uint32_t id)
{
	char buf[32];

	memset(buf, 0, sizeof(buf));
	snprintf(buf, 32, "%u", id);

	return do_sysfs(name, "id", buf);
}
#endif

#if 0
static int update_dir_members(char *name)
{
	char path[PATH_MAX];
	DIR *d;
	struct dirent *de;
	int i = 0;

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s/%s/nodes", SPACES_DIR, name);

	d = opendir(path);
	if (!d) {
		log_debug("%s: opendir failed: %d", path, errno);
		return -1;
	}

	memset(dir_members, 0, sizeof(dir_members));
	dir_members_count = 0;

	/* FIXME: we should probably read the nodeid in each dir instead */

	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		dir_members[i++] = atoi(de->d_name);
		log_debug("dir_member %d", dir_members[i-1]);
	}
	closedir(d);

	dir_members_count = i;
	return 0;
}

static int id_exists(int id, int count, int *array)
{
	int i;
	for (i = 0; i < count; i++) {
		if (array[i] == id)
			return 1;
	}
	return 0;
}
#endif

static int create_path(char *path)
{
	mode_t old_umask;
	int rv;

	old_umask = umask(0022);
	rv = mkdir(path, 0777);
	umask(old_umask);

	if (rv < 0) {
		log_error("%s: mkdir failed: %d", path, errno);
		if (errno == EEXIST)
			rv = 0;
	}
	return rv;
}

static int path_exists(const char *path)
{
	struct stat buf;

	if (stat(path, &buf) < 0) {
		if (errno != ENOENT)
			log_error("%s: stat failed: %d", path, errno);
		return 0;
	}
	return 1;
}

#if 0
static int open_ccs(void)
{
	int i, cd;

	while ((cd = ccs_connect()) < 0) {
		sleep(1);
		if (++i > 9 && !(i % 10))
			log_error("connect to ccs error %d, "
				  "check ccsd or cluster status", cd);
	}
	return cd;
}

/* when not set in cluster.conf, a node's default weight is 1 */

#define WEIGHT_PATH "/cluster/clusternodes/clusternode[@name=\"%s\"]/@weight"

static int get_weight(int cd, int nodeid)
{
	char path[PATH_MAX], *str, *name;
	int error, w;

	name = nodeid2name(nodeid);
	if (!name) {
		log_error("no name for nodeid %d", nodeid);
		return 1;
	}

	memset(path, 0, PATH_MAX);
	sprintf(path, WEIGHT_PATH, name);

	error = ccs_get(cd, path, &str);
	if (error || !str)
		return 1;

	w = atoi(str);
	free(str);
	return w;
}

int set_members(char *name, int new_count, int *new_members)
{
	char path[PATH_MAX];
	char buf[32];
	int i, w, fd, rv, id, cd = 0, old_count, *old_members;

	/*
	 * create lockspace dir if it doesn't exist yet
	 */

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s/%s", SPACES_DIR, name);

	if (!path_exists(path)) {
		if (create_path(path))
			return -1;
	}

	/*
	 * remove/add lockspace members
	 */

	rv = update_dir_members(name);
	if (rv)
		return rv;

	old_members = dir_members;
	old_count = dir_members_count;

	for (i = 0; i < old_count; i++) {
		id = old_members[i];
		if (id_exists(id, new_count, new_members))
			continue;

		memset(path, 0, PATH_MAX);
		snprintf(path, PATH_MAX, "%s/%s/nodes/%d",
			 SPACES_DIR, name, id);

		log_debug("set_members rmdir \"%s\"", path);

		rv = rmdir(path);
		if (rv) {
			log_error("%s: rmdir failed: %d", path, errno);
			goto out;
		}
	}

	/*
	 * remove lockspace dir after we've removed all the nodes
	 * (when we're shutting down and adding no new nodes)
	 */

	if (!new_count) {
		memset(path, 0, PATH_MAX);
		snprintf(path, PATH_MAX, "%s/%s", SPACES_DIR, name);

		log_debug("set_members lockspace rmdir \"%s\"", path);

		rv = rmdir(path);
		if (rv)
			log_error("%s: rmdir failed: %d", path, errno);
	}

	for (i = 0; i < new_count; i++) {
		id = new_members[i];
		if (id_exists(id, old_count, old_members))
			continue;

		if (!is_cman_member(id))
			cman_statechange();
		/*
		 * create node's dir
		 */

		memset(path, 0, PATH_MAX);
		snprintf(path, PATH_MAX, "%s/%s/nodes/%d",
			 SPACES_DIR, name, id);

		log_debug("set_members mkdir \"%s\"", path);

		rv = create_path(path);
		if (rv)
			goto out;

		/*
		 * set node's nodeid
		 */

		memset(path, 0, PATH_MAX);
		snprintf(path, PATH_MAX, "%s/%s/nodes/%d/nodeid",
			 SPACES_DIR, name, id);

		rv = fd = open(path, O_WRONLY);
		if (rv < 0) {
			log_error("%s: open failed: %d", path, errno);
			goto out;
		}

		memset(buf, 0, 32);
		snprintf(buf, 32, "%d", id);

		rv = do_write(fd, buf, strlen(buf));
		if (rv < 0) {
			log_error("%s: write failed: %d, %s", path, errno, buf);
			close(fd);
			goto out;
		}
		close(fd);

		/*
		 * set node's weight
		 */

		if (!cd)
			cd = open_ccs();

		w = get_weight(cd, id);

		memset(path, 0, PATH_MAX);
		snprintf(path, PATH_MAX, "%s/%s/nodes/%d/weight",
			 SPACES_DIR, name, id);

		rv = fd = open(path, O_WRONLY);
		if (rv < 0) {
			log_error("%s: open failed: %d", path, errno);
			goto out;
		}

		memset(buf, 0, 32);
		snprintf(buf, 32, "%d", w);

		rv = do_write(fd, buf, strlen(buf));
		if (rv < 0) {
			log_error("%s: write failed: %d, %s", path, errno, buf);
			close(fd);
			goto out;
		}
		close(fd);
	}

	rv = 0;
 out:
	if (cd)
		ccs_disconnect(cd);
	return rv;
}
#endif

#if 0
char *str_ip(char *addr)
{
	static char ip[256];
	struct sockaddr_in *sin = (struct sockaddr_in *) addr;
	memset(ip, 0, sizeof(ip));
	inet_ntop(AF_INET, &sin->sin_addr, ip, 256);
	return ip;
}
#endif

static char *str_ip(char *addr)
{
	static char str_ip_buf[INET6_ADDRSTRLEN];
	struct sockaddr_storage *ss = (struct sockaddr_storage *)addr;
	struct sockaddr_in *sin = (struct sockaddr_in *)addr;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addr;
	void *saddr;

	if (ss->ss_family == AF_INET6)
		saddr = &sin6->sin6_addr;
	else
		saddr = &sin->sin_addr;

	inet_ntop(ss->ss_family, saddr, str_ip_buf, sizeof(str_ip_buf));
	return str_ip_buf;
}

static char *str_port(char *addr)
{
	static char str_port_buf[32];
	struct sockaddr_storage *ss = (struct sockaddr_storage *)addr;
	struct sockaddr_in *sin = (struct sockaddr_in *)addr;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addr;
	int port;

	if (ss->ss_family == AF_INET6)
		port = ntohs(sin6->sin6_port);
	else
		port = ntohs(sin->sin_port);

	snprintf(str_port_buf, sizeof(str_port_buf), "%d", port);
	return str_port_buf;
}

/* clear out everything under nodes_dir */
static int clear_configfs_nodes(void)
{
	char path[PATH_MAX];
	DIR *d;
	struct dirent *de;
	int rv, failcount = 0;

	d = opendir(nodes_dir);
	if (!d) {
		log_debug("%s: opendir failed: %d", nodes_dir, errno);
		return -1;
	}

	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		snprintf(path, PATH_MAX, "%s/%s", nodes_dir, de->d_name);

		log_debug("clear_configfs_nodes rmdir \"%s\"", path);

		rv = rmdir(path);
		if (rv) {
			log_error("%s: rmdir failed: %d", path, errno);
			failcount++;
		}
	}
	closedir(d);

	return failcount;
}



#if 0
static void clear_configfs_space_nodes(char *name)
{
	char path[PATH_MAX];
	int i, rv;

	rv = update_dir_members(name);
	if (rv < 0)
		return;

	for (i = 0; i < dir_members_count; i++) {
		memset(path, 0, PATH_MAX);
		snprintf(path, PATH_MAX, "%s/%s/nodes/%d",
			 SPACES_DIR, name, dir_members[i]);

		log_debug("clear_configfs_space_nodes rmdir \"%s\"", path);

		rv = rmdir(path);
		if (rv)
			log_error("%s: rmdir failed: %d", path, errno);
	}
}

/* clear out everything under config/dlm/cluster/spaces/ */

void clear_configfs_spaces(void)
{
	char path[PATH_MAX];
	DIR *d;
	struct dirent *de;
	int rv;

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s", SPACES_DIR);

	d = opendir(path);
	if (!d) {
		log_debug("%s: opendir failed: %d", path, errno);
		return;
	}

	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;

		clear_configfs_space_nodes(de->d_name);

		memset(path, 0, PATH_MAX);
		snprintf(path, PATH_MAX, "%s/%s", SPACES_DIR, de->d_name);
		
		log_debug("clear_configfs_spaces rmdir \"%s\"", path);

		rv = rmdir(path);
		if (rv)
			log_error("%s: rmdir failed: %d", path, errno);
	}
	closedir(d);
}
#endif

void clear_configfs(void)
{
	if (!cluster_dir[0] || !nodes_dir[0]) 
		return;

	if (!path_exists(cluster_dir) || !path_exists(nodes_dir))
		return;

	clear_configfs_nodes();
#if 0
	clear_configfs_spaces();
#endif
	rmdir(cluster_dir);
}

static int add_configfs_base(void)
{
	int rv = -1;
	char *cluster_name;

	if (!path_exists("/sys/kernel/config")) {
		log_error("No /sys/kernel/config, is configfs loaded?");
		goto out;
	}

	if (!path_exists("/sys/kernel/config/cluster")) {
		log_error("No /sys/kernel/config/cluster, is ocfs2_nodemanager loaded?");
		goto out;
	}

	if (!nodes_dir[0] || !cluster_dir[0]) {
		cluster_name = get_cluster_name();
		if (!cluster_name)
			goto out;

		snprintf(cluster_dir, PATH_MAX, CLUSTER_FORMAT,
			 cluster_name);
		snprintf(nodes_dir, PATH_MAX, NODES_FORMAT, cluster_name);
	}

	if (!path_exists(cluster_dir)) {
		rv = create_path(cluster_dir);
		if (rv)
			goto out;
	}

	if (!path_exists(nodes_dir)) {
		log_error("Path %s exists, but %s does not.  Is this really ocfs2_nodemanager?",
			  cluster_dir, nodes_dir);
		rv = -1;
	}

out:
	return rv;
}

static int do_set(const char *name, const char *attr, const char *val)
{
	char path[PATH_MAX];
	int fd, rv;

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s/%s/num", nodes_dir, name);

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		log_error("%s: open failed: %d", path, errno);
		return -1;
	}

	rv = do_write(fd, (char *)val, strlen(val));
	if (rv < 0) {
		log_error("%s: write failed: %d, %s", path, errno, val);
		close(fd);
		return -1;
	}
	close(fd);

	return 0;
}

int add_configfs_node(const char *name, int nodeid, char *addr, int addrlen,
		      int local)
{
	char path[PATH_MAX];
	char buf[32];
	int rv;

	log_debug("add_configfs_node %s %d %s local %d",
		  name, nodeid, str_ip(addr), local);

	rv = add_configfs_base();
	if (rv < 0)
		return rv;

	/*
	 * create comm dir for this node
	 */

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s/%s", nodes_dir, name);

	rv = create_path(path);
	if (rv)
		return -1;

	/*
	 * set the nodeid
	 */

	memset(buf, 0, sizeof(buf));
	snprintf(buf, 32, "%d", nodeid);
	rv = do_set(name, "num", buf);
	if (rv < 0)
		return -1;

	/*
	 * set the address
	 */

	rv = do_set(name, "ipv4_addr", str_ip(addr));
	if (rv < 0)
		return -1;

	/*
	 * set the port
	 */

	rv = do_set(name, "ipv4_port", str_port(addr));
	if (rv < 0)
		return -1;

	/*
	 * set local
	 */

	if (local) {
		rv = do_set(name, "local", "1");
		if (rv < 0)
			return -1;
	}

	return 0;
}

void del_configfs_node(const char *name)
{
	char path[PATH_MAX];
	int rv;

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s/%s", nodes_dir, name);

	log_debug("del_configfs_node rmdir \"%s\"", path);

	rv = rmdir(path);
	if (rv)
		log_error("%s: rmdir failed: %d", path, errno);
}

#if 0
#define PROTOCOL_PATH "/cluster/dlm/@protocol"
#define PROTO_TCP  1
#define PROTO_SCTP 2

static int get_ccs_protocol(int cd)
{
	char path[PATH_MAX], *str;
	int error, rv;

	memset(path, 0, PATH_MAX);
	sprintf(path, PROTOCOL_PATH);

	error = ccs_get(cd, path, &str);
	if (error || !str)
		return -1;

	if (!strncasecmp(str, "tcp", 3))
		rv = PROTO_TCP;
	else if (!strncasecmp(str, "sctp", 4))
		rv = PROTO_SCTP;
	else {
		log_error("read invalid dlm protocol from ccs");
		rv = 0;
	}

	free(str);
	log_debug("got ccs protocol %d", rv);
	return rv;
}

#define TIMEWARN_PATH "/cluster/dlm/@timewarn"

static int get_ccs_timewarn(int cd)
{
	char path[PATH_MAX], *str;
	int error, rv;

	memset(path, 0, PATH_MAX);
	sprintf(path, TIMEWARN_PATH);

	error = ccs_get(cd, path, &str);
	if (error || !str)
		return -1;

	rv = atoi(str);

	if (rv <= 0) {
		log_error("read invalid dlm timewarn from ccs");
		rv = -1;
	}

	free(str);
	log_debug("got ccs timewarn %d", rv);
	return rv;
}

#define DEBUG_PATH "/cluster/dlm/@log_debug"

static int get_ccs_debug(int cd)
{
	char path[PATH_MAX], *str;
	int error, rv;

	memset(path, 0, PATH_MAX);
	sprintf(path, DEBUG_PATH);

	error = ccs_get(cd, path, &str);
	if (error || !str)
		return -1;

	rv = atoi(str);

	if (rv < 0) {
		log_error("read invalid dlm log_debug from ccs");
		rv = -1;
	}

	free(str);
	log_debug("got ccs log_debug %d", rv);
	return rv;
}

static int set_configfs_protocol(int proto)
{
	char path[PATH_MAX];
	char buf[32];
	int fd, rv;

	rv = add_configfs_base();
	if (rv < 0)
		return rv;

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s/protocol", CLUSTER_DIR);

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		log_error("%s: open failed: %d", path, errno);
		return fd;
	}

	memset(buf, 0, sizeof(buf));
	snprintf(buf, 32, "%d", proto);

	rv = do_write(fd, buf, strlen(buf));
	if (rv < 0) {
		log_error("%s: write failed: %d", path, errno);
		return rv;
	}
	close(fd);
	log_debug("set protocol %d", proto);
	return 0;
}

static int set_configfs_timewarn(int cs)
{
	char path[PATH_MAX];
	char buf[32];
	int fd, rv;

	rv = add_configfs_base();
	if (rv < 0)
		return rv;

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s/timewarn_cs", CLUSTER_DIR);

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		log_error("%s: open failed: %d", path, errno);
		return fd;
	}

	memset(buf, 0, sizeof(buf));
	snprintf(buf, 32, "%d", cs);

	rv = do_write(fd, buf, strlen(buf));
	if (rv < 0) {
		log_error("%s: write failed: %d", path, errno);
		return rv;
	}
	close(fd);
	log_debug("set timewarn_cs %d", cs);
	return 0;
}

static int set_configfs_debug(int val)
{
	char path[PATH_MAX];
	char buf[32];
	int fd, rv;

	rv = add_configfs_base();
	if (rv < 0)
		return rv;

	memset(path, 0, PATH_MAX);
	snprintf(path, PATH_MAX, "%s/log_debug", CLUSTER_DIR);

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		log_error("%s: open failed: %d", path, errno);
		return fd;
	}

	memset(buf, 0, sizeof(buf));
	snprintf(buf, 32, "%d", val);

	rv = do_write(fd, buf, strlen(buf));
	if (rv < 0) {
		log_error("%s: write failed: %d", path, errno);
		return rv;
	}
	close(fd);
	log_debug("set log_debug %d", val);
	return 0;
}

static void set_protocol(int cd)
{
	int rv, proto;

	rv = get_ccs_protocol(cd);
	if (!rv || rv < 0)
		return;

	/* for dlm kernel, TCP=0 and SCTP=1 */
	if (rv == PROTO_TCP)
		proto = 0;
	else if (rv == PROTO_SCTP)
		proto = 1;
	else
		return;

	set_configfs_protocol(proto);
}

static void set_timewarn(int cd)
{
	int rv;

	rv = get_ccs_timewarn(cd);
	if (rv < 0)
		return;

	set_configfs_timewarn(rv);
}

static void set_debug(int cd)
{
	int rv;

	rv = get_ccs_debug(cd);
	if (rv < 0)
		return;

	set_configfs_debug(rv);
}

void set_ccs_options(void)
{
	int cd;

	cd = open_ccs();

	log_debug("set_ccs_options %d", cd);

	set_protocol(cd);
	set_timewarn(cd);
	set_debug(cd);

	ccs_disconnect(cd);
}
#endif
