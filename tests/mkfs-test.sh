#!/bin/bash
#
# mkfs-test.sh - Test mkfs.ocfs2
#
# Copyright (C) 2011, 2012 Oracle.  All rights reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License, version 2,  as published by the Free Software Foundation.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
# 
################################################################

APP=$(basename ${0})
PATH=$PATH:$(dirname ${0}):/sbin

CHOWN=$(which chown)
DATE=$(which date)
ECHO=$(which echo)
MKDIR=$(which mkdir)
SEQ=$(which seq)
AWK=$(which awk)
SUDO="$(which sudo) -u root"
WHOAMI=$(which whoami)

USERID=$(${WHOAMI})

MKFS_BIN="${SUDO} `which mkfs.ocfs2`"
FSCK_BIN="${SUDO} $(which fsck.ocfs2)"
TUNE_BIN="${SUDO} $(which tunefs.ocfs2)"
DEBUG_BIN="${SUDO} $(which debugfs.ocfs2)"

# log_message message
log_message()
{
	${ECHO} "`${DATE}  +\"%F %H:%M:%S\"` $@"
	${ECHO} "`${DATE}  +\"%F %H:%M:%S\"` $@" >> ${LOGFILE}
}

log_start()
{
	log_message $@
	START=$(date +%s)
}

# log_end $?
log_end()
{
	if [ "$#" -lt "1" ]; then
      		${ECHO} "Error in log_end()"
		exit 1
	fi

	rc=$1
	shift

	END=$(date +%s)
	DIFF=$(( ${END} - ${START} ))

	if [ $rc -ne 0 ]; then
		log_message "$@ (${DIFF} secs) ** FAIL **"
	else
		log_message "$@ (${DIFF} secs) succ"
	fi

	START=0
}

# epilogue rc
epilogue()
{
	if [ "$#" -lt "1" ]; then
		${ECHO} "Error in epilogue()"
		exit 1
	fi

	rc=$1

	if [ $rc -eq 0 ]
	then
		PASS=$[$PASS + 1];
	else
		FAIL=$[$FAIL + 1];
	fi
	TESTNUM=$[${TESTNUM} + 1]
}

# fsck_volume device outlog
fsck_volume()
{
	if [ "$#" -lt "2" ]; then
      		${ECHO} "Error in fsck_volume() $@"
		exit 1
	fi

	device=$1
	outlog=$2

	cmd="${FSCK_BIN} -fy ${device}"

	$($cmd >>${outlog} 2>&1)
	RET=$?
	if [ $RET -ne 0 ]; then
		${ECHO} "$cmd" >>${outlog}
		${ECHO} "ERROR: Failed with ${RET}" >>${outlog}
		return 1
	fi
	return 0
}

# verify_block_cluster blksz cltsz device outlog
verify_block_cluster()
{
	if [ "$#" -lt "4" ]; then
		${ECHO} "Error in verify_block_cluster() $@"
		exit 1
	fi

	blksz=$1
	cltsz=$2
	device=$3
	outlog=$4

	out=$(${TUNE_BIN} -Q "B=%B;C=%T;" ${device} 2>&1)
	echo ${out} >> ${outlog}
	if [ "${out}" != "B=${blksz};C=${cltsz};" ]
	then
		${ECHO} "ERROR: Incorrect block or cluster size" >>${outlog}
		return 1
	fi
	return 0
}

# verify_journal_size jrnlsz device outlog
verify_journal_size()
{
	if [ "$#" -lt "3" ]; then
		${ECHO} "Error in verify_journal_size() $@"
		exit 1
	fi

	jrnlsz=$1
	device=$2
	outlog=$3

	${DEBUG_BIN} -R "stat //journal:0000" ${device} >>${outlog} 2>&1
	size=$(${AWK} '/User:/ {print $8}' $outlog)
	if [ "${size}" != "$[${jrnlsz} * 1024 * 1024]" ]
	then
		${ECHO} "ERROR: Incorrect journal size" >>${outlog}
		return 1
	fi
	return 0
}

# verify_node_slots slots device outlog
verify_node_slots()
{
	if [ "$#" -lt "3" ]; then
		${ECHO} "Error in verify_node_slots() $@"
		exit 1
	fi

	slots=$1
	device=$2
	outlog=$3

	out=$(${TUNE_BIN} -Q "N=%N;" ${device} 2>&1)
	echo ${out} >> ${outlog}
	if [ "${out}" != "N=${slots};" ]
	then
		${ECHO} "ERROR: Incorrect slots" >>${outlog}
		return 1
	fi
	return 0
}

# verify_vol_label label device outlog
verify_vol_label()
{
	if [ "$#" -lt "3" ]; then
		${ECHO} "Error in verify_vol_label() $@"
		exit 1
	fi

	label=$1
	device=$2
	outlog=$3

	out=$(${TUNE_BIN} -Q "V=%V;" ${device} 2>&1)
	echo ${out} >> ${outlog}
	if [ "${out}" != "V=${label};" ]
	then
		${ECHO} "ERROR: Incorrect label" >>${outlog}
		return 1
	fi
	return 0
}

# do_mkdir dir
do_mkdir()
{
	if [ "$#" -lt "1" ]; then
		${ECHO} "Error in do_mkdir()"
		exit 1
	fi

	${SUDO} ${MKDIR} -p $1
	if [ $? -ne 0 ]; then
		${ECHO} "ERROR: mkdir $1"
		exit 1
	fi

	${SUDO} ${CHOWN} -R ${USERID} $1
}

#
#
# MAIN
#
#

usage()
{
	${ECHO} "usage: ${APP} -m path-to-mkfs -l logdir -d device"
	exit 1
}

while getopts "m:d:l:h?" args
do
	case "$args" in
		m) MKFS_BIN="$OPTARG";;
		d) DEVICE="$OPTARG";;
		l) OUTDIR="$OPTARG";;
    		h) usage;;
    		?) usage;;
  	esac
done

if [ -z ${DEVICE} ] ; then
	${ECHO} "ERROR: No device"
	usage
elif [ ! -b ${DEVICE} ] ; then
	${ECHO} "ERROR: Invalid device ${DEVICE}"
	exit 1
fi

if [ -z ${OUTDIR} ]; then
	${ECHO} "ERROR: No logdir"
	usage
fi

RUNDATE=`${DATE} +%F_%H:%M`
LOGDIR=${OUTDIR}/${RUNDATE}
LOGFILE=${LOGDIR}/mkfs_test.log

do_mkdir ${LOGDIR}

${ECHO} "Output log is ${LOGFILE}"

STARTRUN=$(date +%s)
log_message "*** Start mkfs test ***"

FAIL=0
PASS=0

TESTNUM=1

### Test all combinations of blocksizes and clustersizes
for blksz in 512 1024 2048 4096
do
	for cltsz in 4096 8192 16384 32768 65536 131072 262144 524288 1048576
	do
		TEST="Test ${TESTNUM}: -b ${blksz} -C ${cltsz}"
		log_start "${TEST}"
		OUTLOG=${LOGDIR}/mkfs_test_${TESTNUM}.out

		# format
		${MKFS_BIN} -x -M local -L mkfstest -b ${blksz} -C ${cltsz} ${DEVICE} >>${OUTLOG} 2>&1
		rc=$?
		if [ $rc -eq 0 ]
		then
			# fsck
			fsck_volume "${DEVICE}" "${OUTLOG}"
			rc=$?
			if [ $rc -eq 0 ]
			then
				verify_block_cluster ${blksz} ${cltsz} "${DEVICE}" "${OUTLOG}"
				rc=$?
			fi
		fi

		log_end $rc "${TEST}"

		epilogue $rc
	done
done

### Test -J size 4M, 64M, 128M, 256M
for jrnlsz in 4 64 128 256
do
	TEST="Test ${TESTNUM}: -J size=${jrnlsz}M"
	log_start "${TEST}"
	OUTLOG=${LOGDIR}/mkfs_test_${TESTNUM}.out

	# format
	${MKFS_BIN} -x -M local -L mkfstest -J size=${jrnlsz}M ${DEVICE} >>${OUTLOG} 2>&1
	rc=$?
	if [ $rc -eq 0 ]
	then
		# fsck
		fsck_volume "${DEVICE}" "${OUTLOG}"
		rc=$?
		if [ $rc -eq 0 ]
		then
			verify_journal_size ${jrnlsz} "${DEVICE}" "${OUTLOG}"
			rc=$?
		fi
	fi

	log_end $rc "${TEST}"

	epilogue $rc
done

### Test -N 2 8 16 32 64 128 255
for slots in 2 8 16 32 64 128 255
do
	TEST="Test ${TESTNUM}: -N ${slots}"
	log_start "${TEST}"
	OUTLOG=${LOGDIR}/mkfs_test_${TESTNUM}.out

	# format
	${MKFS_BIN} -x -M local -L mkfstest -J size=4M -N ${slots} ${DEVICE} >>${OUTLOG} 2>&1
	rc=$?
	if [ $rc -eq 0 ]
	then
		# fsck
		fsck_volume "${DEVICE}" "${OUTLOG}"
		rc=$?
		if [ $rc -eq 0 ]
		then
			verify_node_slots ${slots} "${DEVICE}" "${OUTLOG}"
			rc=$?
		fi
	fi

	log_end $rc "${TEST}"

	epilogue $rc
done

#### Test -T fstype
for fstype in mail datafiles vmstore
do
	TEST="Test ${TESTNUM}: -T ${fstype}"
	log_start "${TEST}"
	OUTLOG=${LOGDIR}/mkfs_test_${TESTNUM}.out

	# format
	${MKFS_BIN} -x -M local -L mkfstest -J size=4M -T ${fstype} ${DEVICE} >>${OUTLOG} 2>&1
	rc=$?
	if [ $rc -eq 0 ]
	then
		# fsck
		fsck_volume "${DEVICE}" "${OUTLOG}"
		rc=$?
	fi

	log_end $rc "${TEST}"

	epilogue $rc
done

#### Test -L label
for label in mylabel label6789012345678901234567890123456789012345678901234567890123
do
	TEST="Test ${TESTNUM}: -L ${label}"
	log_start "${TEST}"
	OUTLOG=${LOGDIR}/mkfs_test_${TESTNUM}.out

	# format
	${MKFS_BIN} -x -M local -L ${label} -J size=4M ${DEVICE} >>${OUTLOG} 2>&1
	rc=$?
	if [ $rc -eq 0 ]
	then
		# fsck
		fsck_volume "${DEVICE}" "${OUTLOG}"
		rc=$?
		if [ $rc -eq 0 ]
		then
			verify_vol_label "${label}" "${DEVICE}" "${OUTLOG}"
			rc=$?
		fi
	fi

	log_end $rc "${TEST}"

	epilogue $rc
done

##### Test -U uuid
for uuid in 2A4D1C581FAA42A1A41D26EFC90C1315 2a4d1c58-1faa-42a1-a41d-26efc90c1315
do
	TEST="Test ${TESTNUM}: -U ${uuid}"
	log_start "${TEST}"
	OUTLOG=${LOGDIR}/mkfs_test_${TESTNUM}.out

	# format
	${MKFS_BIN} -x -M local -L mkfstest -U ${uuid} -J size=4M ${DEVICE} >>${OUTLOG} 2>&1
	rc=$?
	if [ $rc -eq 0 ]
	then
		# fsck
		fsck_volume "${DEVICE}" "${OUTLOG}"
		rc=$?
	fi

	log_end $rc "${TEST}"

	epilogue $rc
done

ENDRUN=$(date +%s)

DIFF=$(( ${ENDRUN} - ${STARTRUN} ))
log_message "Pass: $PASS  Fail: $FAIL"
log_message "Total Runtime ${DIFF} seconds"
