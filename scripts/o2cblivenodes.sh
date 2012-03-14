#!/bin/sh
#
# o2cblivenodes
#
# Prints list of nodes heartbeating on disk and network in a o2cb cluster
#
# Copyright (C) 2010, 2012 Oracle.  All rights reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#

export LC_ALL=C

MOUNT=$(which mount)
AWK=$(which awk)
CAT=$(which cat)
ECHO=$(which echo)
GREP=$(which grep)

DBGMNT="/sys/kernel/debug"
CFGMNT="/sys/kernel/config"

mount_vol()
{
	if [ "$#" -lt "2" ]; then
		${ECHO} "mount_vol(): Requires more arguments" >&2
		exit 1
	fi

	FS=$1
	MPATH=$2

	mpath=$(${MOUNT} -t ${FS} | ${GREP} "^${FS} " | cut -f3 -d' ')
	if [ -z ${mpath} -o ${mpath} != ${MPATH} ]; then
		$MOUNT} -t ${FS} ${FS} ${MPATH}
		mpath=$(${MOUNT} -t ${FS} | ${GREP} "^${FS} " | cut -f3 -d' ')
	fi

	if [ -z ${mpath} ]; then
		${ECHO} "mount_vol(): Unable to mount ${FS} at ${MPATH}" >&2
		exit 1
	fi
}

# ensure configfs and debugfs are mounted
mount_vol debugfs ${DBGMNT}
mount_vol configfs ${CFGMNT}

# find the cluster name
CONFIGURATION=/etc/sysconfig/o2cb
[ -f "${CONFIGURATION}" ] && . "${CONFIGURATION}"

if [ -z "${O2CB_BOOTCLUSTER}" ]; then
	${ECHO} "No o2cb cluster configured"
	exit 1
fi

# Exit if o2cb cluster not found
if [ ! -d ${CFGMNT}/cluster/${O2CB_BOOTCLUSTER} ]; then
	${ECHO} "No active o2cb cluster found"
	exit 1
fi

# get the node number of this node
for i in ${CFGMNT}/cluster/${O2CB_BOOTCLUSTER}/node/*
do
	local=$(${CAT} $i/local);
	if [ $local -eq 1 ]
	then
		NODENUM=$(${CAT} $i/num);
		break;
	fi;
done;

# list of nodenums heartbeating on disk
disknodes=$(${CAT} ${DBGMNT}/o2hb/livenodes 2>/dev/null)

if [ -z "$disknodes" ]; then
	${ECHO} "No live nodes found for cluster ${O2CB_BOOTCLUSTER}"
	exit 1
fi

# strip out this nodenum from above
DISK=""
for i in $disknodes
do
	if [ $i -ne $NODENUM ]; then
		DISK=$(${ECHO} $DISK $i)
	fi
done

# list of nodenames hearbeating on net
netnodes=$(${AWK} '
	BEGIN { for (i=0; i<255; i++) N[i]=""; n=0; }
	/remote node:/ { N[n] = $3; n++; }
	END { for (i=0; i<n; ++i) printf("%s ", N[i]); }' ${DBGMNT}/o2net/sock_containers)

# generate list of nodenums heartbeating on net
NET=""
for i in $netnodes
do
	num=$(${CAT} ${CFGMNT}/cluster/${O2CB_BOOTCLUSTER}/node/${i}/num)
	NET=$(${ECHO} $NET $num)
done

# list of nodes that are hb on disk but not on net
MISS=""
for i in $DISK
do
	if [ -z "$NET" ]; then
		MISS=$(${ECHO} $MISS $i)
		continue;
	fi
	found=0
	for j in $NET
	do
		if [ ${i} -eq ${j} ]; then
			found=1
			break;
		fi
	done
	if [ ${found} -eq 0 ]; then
		MISS=$(${ECHO} $MISS $i)
	fi
done

${ECHO} "Cluster : $O2CB_BOOTCLUSTER"
${ECHO} "Node    : $NODENUM"
${ECHO} "Alive   : $DISK"
${ECHO} "Connects: $NET"
${ECHO} -n "Summary : "
if [ -z "${MISS}" ]; then
	${ECHO} "Node $NODENUM is connected to all live nodes"
else
	${ECHO} "Node $NODENUM is missing connections to nodes $MISS"
fi
