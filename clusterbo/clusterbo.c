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

#define __u8 unsigned char 
#define u8 unsigned char 
#define u16 unsigned short int
#define u32 unsigned int       
#define u64 unsigned long long
#define atomic_t int
#define spinlock_t unsigned long
typedef unsigned short kdev_t;

typedef struct list_head {
        struct list_head *next, *prev;
} list_t;

#include "ocfs2_nodemanager.h"

#define NIPQUAD(addr) \
        ((unsigned char *)&addr)[0], \
        ((unsigned char *)&addr)[1], \
        ((unsigned char *)&addr)[2], \
        ((unsigned char *)&addr)[3]


#define CLUSTER_FILE   "/proc/cluster/nm/.cluster"
#define GROUP_FILE     "/proc/cluster/nm/.group"
#define NODE_FILE      "/proc/cluster/nm/.node"

#define CONF_FILE   "/etc/cluster.conf"

/* are these right ? */
#define MIN_PORT_NUM   1024
#define MAX_PORT_NUM   65535

#define  NET_IOC_MAGIC          'O'
#define  NET_IOC_ACTIVATE       _IOR(NET_IOC_MAGIC, 1, net_ioc)
#define  NET_IOC_GETSTATE       _IOR(NET_IOC_MAGIC, 2, net_ioc)


typedef struct _net_ioc
{
	unsigned int status;
} net_ioc;

#define OCFS2_NM_MODULE  "ocfs2_nodemanager"
#define OCFS2_HB_MODULE  "ocfs2_heartbeat"
#define OCFS2_TCP_MODULE "ocfs2_tcp"


int activate_cluster(void);
int add_node(nm_node_info *newnode);
int set_cluster_name(char *cluster_name);
int activate_net(void);
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
		u32 real_ip;

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
	
	set_cluster_name(cluster_name);
	i=0;
	while (1) {
		if (!total_nodes--)
			break;
		if (!nodes[i].node_name[0]) {
			i++;
			continue;
		}
		add_node(&nodes[i]);
		i++;
	}
	printf("done.  activating cluster now...\n");
	activate_cluster();
	printf("done.  nm ready!\n");
	activate_net();
	printf("done.  net ready!\n");
	free(nodes);
	return 0;
}

int set_cluster_name(char *cluster_name)
{
	int fd;
	nm_op *op;
	int ret;
	char *buf;
	
	buf = malloc(4096);
	op = (nm_op *)buf;
	memset(buf, 0, 4096);
	op->magic = NM_OP_MAGIC;

	printf("setting cluster name...\n");
	fd = open(CLUSTER_FILE, O_RDWR);
	if (fd == -1) {
		printf("failed to open %s\n", CLUSTER_FILE);
		exit(1);
	}
	op->opcode = NM_OP_NAME_CLUSTER;
	strcpy(&op->arg_u.name[0], cluster_name);

	ret = write(fd, op, sizeof(nm_op));
	printf("write called returned %d\n", ret);
	if (ret < 0) {
		printf("error is: %s\n", strerror(errno));
		exit(1);
	}
	memset(buf, 0, 4096);
	ret = read(fd, buf, 4096);
	printf("read returned %d\n", ret);
	if (ret < 0)
		exit(1);
	printf("<<<<%*s>>>>\n", ret, buf);
	close(fd);
	free(buf);
	return 0;

}

int add_node(nm_node_info *newnode)
{
	int fd;
	nm_op *op;
	int ret;
	char *buf;
	nm_node_info *node;
	
	buf = malloc(4096);
	op = (nm_op *)buf;
	memset(buf, 0, 4096);
	op->magic = NM_OP_MAGIC;


	printf("adding cluster node....\n");
	fd = open(CLUSTER_FILE, O_RDWR);
	if (fd == -1) {
		printf("failed to open %s\n", CLUSTER_FILE);
		exit(1);
	}
	op->opcode = NM_OP_ADD_CLUSTER_NODE;
	node = &(op->arg_u.node);
	memcpy(node, newnode, sizeof(nm_node_info));
	printf("passing port=%u, vers=%u, addr=%d.%d.%d.%d\n",
	       node->ifaces[0].ip_port,
	       node->ifaces[0].ip_version,
	       NIPQUAD(node->ifaces[0].addr_u.ip_addr4));

	ret = write(fd, op, sizeof(nm_op));
	printf("write called returned %d\n", ret);
	if (ret < 0) {
		printf("error is: %s\n", strerror(errno));
		exit(1);
	}
	memset(buf, 0, 4096);
	ret = read(fd, buf, 4096);
	printf("read returned %d\n", ret);
	if (ret < 0)
		exit(1);
	printf("<<<<%*s>>>>\n", ret, buf);
	close(fd);

	free(buf);
	return 0;

}

int activate_cluster(void)
{
	int fd;
	nm_op *op;
	int ret;
	char *buf;
	
	buf = malloc(4096);
	op = (nm_op *)buf;
	memset(buf, 0, 4096);
	op->magic = NM_OP_MAGIC;

	printf("activating cluster....\n");
	fd = open(CLUSTER_FILE, O_RDWR);
	if (fd == -1) {
		printf("failed to open %s\n", CLUSTER_FILE);
		exit(1);
	}
	op->opcode = NM_OP_CREATE_CLUSTER;

	ret = write(fd, op, sizeof(nm_op));
	printf("write called returned %d\n", ret);
	if (ret < 0) {
		printf("error is: %s\n", strerror(errno));
		exit(1);
	}
	memset(buf, 0, 4096);
	ret = read(fd, buf, 4096);
	printf("read returned %d\n", ret);
	if (ret < 0)
		exit(1);
	printf("<<<<%*s>>>>\n", ret, buf);
	close(fd);

	free(buf);
	return 0;

}


int activate_net(void)
{
	int fd;
	net_ioc net;

	memset(&net, 0, sizeof(net_ioc));
	fd = open("/proc/cluster/net", O_RDONLY);
	if (fd == -1) {
		printf("eeek.  failed to open\n");
		exit(1);
	}
	
	if (ioctl(fd, NET_IOC_ACTIVATE, &net) == -1) {
		printf("eeek.  ioctl failed\n");
		close(fd);
		exit(1);
	}
	close(fd);
	printf("ioctl returned: %u\n", net.status);
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
