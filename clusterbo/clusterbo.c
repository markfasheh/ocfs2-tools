#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <asm/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "o2cb.h"

#define atomic_t int
#define spinlock_t unsigned long
typedef unsigned short kdev_t;

typedef struct list_head {
        struct list_head *next, *prev;
} list_t;

#define NIPQUAD(addr) \
        ((unsigned char *)&addr)[0], \
        ((unsigned char *)&addr)[1], \
        ((unsigned char *)&addr)[2], \
        ((unsigned char *)&addr)[3]


#define CONF_FILE   "/etc/cluster.conf"

/* are these right ? */
#define MIN_PORT_NUM   1024
#define MAX_PORT_NUM   65535

#define OCFS2_NM_MODULE  "ocfs2_nodemanager"
#define OCFS2_HB_MODULE  "ocfs2_heartbeat"
#define OCFS2_TCP_MODULE "ocfs2_tcp"


int load_module(char *module, char *mountpoint, char *fstype);


nm_node_info *nodes;
int total_nodes = 0;

int main(int argc, char **argv)
{
	int ret, i;
	FILE *conf;
	char *cluster_name = NULL;

	ret = load_module(OCFS2_NM_MODULE, "/proc/cluster/nm", "nm");
	if (ret) {
		fprintf(stderr, "failed to load and/or mount nm: %d\n", ret);
		exit(1);
	}
	ret = load_module(OCFS2_HB_MODULE, "/proc/cluster/heartbeat", "hb");
	if (ret) {
		fprintf(stderr, "failed to load and/or mount hb: %d\n", ret);
		exit(1);
	}
	ret = load_module(OCFS2_TCP_MODULE, NULL, NULL);
	if (ret) {
		fprintf(stderr, "failed to load tcp: %d\n", ret);
		exit(1);
	}

	nodes = malloc(NM_MAX_NODES * sizeof(nm_node_info));
	if (!nodes) {
		fprintf(stderr, "failed to malloc node array\n");
		exit(1);
	}
	memset(nodes, 0, NM_MAX_NODES * sizeof(nm_node_info));
	
	conf = fopen(CONF_FILE, "r");
	if (!conf) {
		fprintf(stderr, "failed to open %s: %s\n", CONF_FILE, strerror(errno));
		exit(1);
	}

	ret = fscanf(conf, "cluster_name=%64as\n", &cluster_name);
	if (ret != 1) {
		fprintf(stderr, "bad file format: expected cluster_name=XXX\n");
		exit(1);
	}
	printf("found cluster named %s\n", cluster_name);

	while (1) {
		int node_num, port;
		char *node_name = NULL;
		char *ip = NULL;
		uint32_t real_ip;

		ret = fscanf(conf, "%d,%64a[^,],%15a[0-9.],%d\n", &node_num, &node_name, &ip, &port);
		if (ret == 0 || ret == -1) {
			printf("done.  found %d nodes\n", total_nodes);
			break;
		}
		if (ret != 4) {
			fprintf(stderr, "bad file format: node_num,node_name,ipaddr,ipport\n");
			exit(1);
		}

		if (node_num < 0 || node_num >= NM_MAX_NODES) {
			fprintf(stderr, "bad node number: got %d, range is 0 - %d\n", node_num, NM_MAX_NODES-1);
			exit(1);
		}
		if (nodes[node_num].node_name[0]) {
			fprintf(stderr, "already have a node in slot %d: orig=%s, this=%s\n", 
				node_num, nodes[node_num].node_name, node_name);
			exit(1);
		}
		if (port < MIN_PORT_NUM || port > MAX_PORT_NUM) {
			fprintf(stderr, "bad port number: got %d, range is %d - %d\n", port, MIN_PORT_NUM, MAX_PORT_NUM);
			exit(1);
		}
		if (!inet_aton(ip, (struct in_addr*)&real_ip)) {
			fprintf(stderr, "bad ipv4 address: %s\n", ip);
			exit(1);
		}


		total_nodes++;
		nodes[node_num].node_num = node_num;
		memcpy(nodes[node_num].node_name, node_name, NM_MAX_NAME_LEN);
		nodes[node_num].node_name[NM_MAX_NAME_LEN]=0;
		nodes[node_num].ifaces[0].ip_port = htons(port);
		nodes[node_num].ifaces[0].addr_u.ip_addr4 = real_ip;
		free(ip);
		free(node_name);
	}
	fclose(conf);
	
	ret = o2cb_set_cluster_name(cluster_name);
	i=0;
	while (1) {
		if (!total_nodes--)
			break;
		if (!nodes[i].node_name[0]) {
			i++;
			continue;
		}
		ret = o2cb_add_node(&nodes[i]);
		i++;
	}
	printf("done.  activating cluster now...\n");
	ret = o2cb_activate_cluster();
	printf("done.  nm ready!\n");
	ret = o2cb_activate_networking();
	printf("done.  net ready!\n");
	free(nodes);
	return 0;
}

int load_module(char *module, char *mountpoint, char *fstype)
{
	int ret;
	int pid;
	int status;
	struct stat st;

	pid = fork();
	switch (pid) {
		case 0:
			ret = execl("/sbin/modprobe", "/sbin/modprobe", module, (char *)NULL);
			fprintf(stderr, "eeek! exec returned %d: %s\n", ret, strerror(errno));
			exit(1);
			break;
		case -1:
			fprintf(stderr, "fork failed: %s\n", strerror(errno));
			return -errno;

		default:
			ret = wait(&status);
			if (WIFEXITED(status)) {
				if (WEXITSTATUS(status) != 0) {
					fprintf(stderr, "modprobe returned %d!\n", WEXITSTATUS(status));
					return -WEXITSTATUS(status);
				}
			} else {
				fprintf(stderr, "modprobe has not exited!\n");
				return -EINVAL;
			}
			break;
	}
	if (!mountpoint)
		return 0;

	if (stat("/proc/cluster", &st) != 0 ||
	    stat(mountpoint, &st) != 0) {
		fprintf(stderr, "mountpoint %s does not exist!\n", mountpoint);
		return -EINVAL;
	}

	ret = mount("none", mountpoint, fstype, 0, "");
	return ret;
}
