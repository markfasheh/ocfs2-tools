#!/bin/bash
#
# fsck-test.sh - Test fsck.ocfs2
#
# This script tests fsck.ocfs2's ability to fix a corrupted volume.
# It uses the fswreck utility to corrupt the volume.
#
# Copyright (C) 2011 Oracle.  All rights reserved.
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
SUDO="$(which sudo) -u root"
WHOAMI=$(which whoami)

USERID=$(${WHOAMI})

MKFS_BIN="${SUDO} `which mkfs.ocfs2`"
FSCK_BIN="${SUDO} $(which fsck.ocfs2)"
FSWRECK_BIN="${SUDO} $(which fswreck)"

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

get_bits()
{
	if [ "$#" -lt "1" ]; then
      		${ECHO} "Error in get_bits()"
		exit 1
	fi

	val=$1

	for i in `${SEQ} 1 31`
	do
		if [ $[2 ** $i] -eq ${val} ]; then
			return $i
		fi
	done

	exit 1
}

# do_format() code device outlog
do_format()
{
	if [ "$#" -lt "3" ]; then
      		${ECHO} "Error in do_format() $@"
		exit 1
	fi

	code=$1
	device=$2
	outlog=$3

	MKFSOPTS=$(${FSWRECK_BIN} -C ${code} -M 2>>${outlog})
	RET=$?
	if [ $RET -ne 0 ]
	then
		${ECHO} "ERROR: fswreck -C ${code} failed with ${RET}" >>${outlog}
		exit 1
	fi

	cmd="${MKFS_BIN} -x ${MKFSOPTS} -L fswreck ${device}"

	$($cmd >>${outlog} 2>&1)
	RET=$?
	if [ $RET -ne 0 ]; then
		${ECHO} "$cmd" >>${outlog}
		${ECHO} "ERROR: Failed with ${RET}" >>${outlog}
		exit 1
	fi
}

# do_fswreck() corruptcode device outlog
do_fswreck()
{
	if [ "$#" -lt "3" ]; then
      		${ECHO} "Error in do_fswreck() $@"
		exit 1
	fi

	corruptcode=$1
	device=$2
	outlog=$3

	cmd="${FSWRECK_BIN} -C ${corruptcode} ${device}"

	$($cmd >>${outlog} 2>&1)
	RET=$?
	if [ $RET -ne 0 ]; then
		${ECHO} "$cmd" >>${outlog}
		${ECHO} "ERROR: Failed with ${RET}" >>${outlog}
		return 1
	fi
	return 0
}

# do_fsck() device outlog
do_fsck()
{
	if [ "$#" -lt "2" ]; then
      		${ECHO} "Error in do_fsck() $@"
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

# do_mkdir DIR
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
	${ECHO} -n "usage: ${APP} -f path-to-fswreck -F path-to-fsck "
	${ECHO}    "-s startcode -e endcode -l logdir -d device"
	exit 1
}

STARTCODE=-1
ENDCODE=-1
MAXCODE=500

while getopts "f:F:d:l:s:e:h?" args
do
	case "$args" in
		f) FSWRECK_BIN="$OPTARG";;
		F) FSCK_BIN="$OPTARG";;
		d) DEVICE="$OPTARG";;
		l) OUTDIR="$OPTARG";;
		s) STARTCODE="$OPTARG";;
		e) ENDCODE="$OPTARG";;
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

if [ ${STARTCODE} -eq -1 ]; then
	STARTCODE=0
fi

if [ ${ENDCODE} -eq -1 ]; then
	ENDCODE=${MAXCODE}
fi

if [ ${ENDCODE} -lt ${STARTCODE} ]; then
	ENDCODE=${STARTCODE}
fi

RUNDATE=`${DATE} +%F_%H:%M`
LOGDIR=${OUTDIR}/${RUNDATE}
LOGFILE=${LOGDIR}/fsck_test.log

do_mkdir ${LOGDIR}

${ECHO} "Output log is ${LOGFILE}"

STARTRUN=$(date +%s)
log_message "*** Start fsck test ***"

FAIL=0
PASS=0

for code in $(seq ${STARTCODE} ${ENDCODE})
do
	# Check code validity
	CODE=$(${FSWRECK_BIN} -L ${code} 2>>${LOGFILE})
	rc=$?
	if [ $rc -ne 0 ]
	then
		break
	fi

	log_start "Corrupt code $code $CODE"
	OUTLOG=${LOGDIR}/corrupt_code_${code}.out

	# Format failure stops the test
	do_format ${code} ${DEVICE} ${OUTLOG}

	# Run fsck only if fswreck succeeds
	do_fswreck ${code} ${DEVICE} ${OUTLOG}
	rc=$?
	if [ $rc -eq 0 ]
	then
		do_fsck ${DEVICE} ${OUTLOG}
		rc=$?
	fi
	log_end $rc "Corrupt code $code $CODE"
	if [ $rc -eq 0 ]
	then
		PASS=$[$PASS + 1];
	else
		FAIL=$[$FAIL + 1];
	fi
done

ENDRUN=$(date +%s)

DIFF=$(( ${ENDRUN} - ${STARTRUN} ))
log_message "*** End Single Node test ***"
log_message "Pass: $PASS  Fail: $FAIL"
log_message "Total Runtime ${DIFF} seconds"
