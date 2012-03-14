#!/bin/bash
#
# puncher-test.sh - Test puncher
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
DD=$(which dd)
MD5SUM=$(which md5sum)
CUT=$(which cut)
STAT=$(which stat)

USERID=$(${WHOAMI})

PUNCHER_BIN=$(which puncher)

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

# punch_holes expectedtoshrink maxcompact testfile outlog
punch_holes()
{
	if [ "$#" -lt "4" ]; then
		${ECHO} "Error in punch_holes() $@"
		exit 1
	fi

	shrink=$1
	compact=$2
	testfile=$3
	outlog=$4

	maxcompact=
	if [ $compact -eq 1 ]; then
		maxcompact="--max-compact"
	fi

	bsize=$(${STAT} --printf="%b\n" ${testfile}) >>${outlog} 2>&1

	bcsum=$(${MD5SUM} ${testfile} | ${CUT} -f1 -d' ') >>${outlog} 2>&1
	rc=$?
	if [ $rc -eq 0 ]; then
		${PUNCHER_BIN} -vv --punch-holes $maxcompact ${testfile} >>${outlog} 2>&1
		rc=$?
	fi
	if [ $rc -eq 0 ]; then
		acsum=$(${MD5SUM} ${testfile} | ${CUT} -f1 -d' ') >>${outlog} 2>&1
		rc=$?
	fi
	if [ $rc -eq 0 ]; then
		if [ "x$bcsum" != "x$acsum" ]; then
			rc=1
		fi
	fi
	asize=$(${STAT} --printf="%b\n" ${testfile}) >>${outlog} 2>&1

	${ECHO} "Checksums are $bcsum and $acsum" >> ${outlog}
	${ECHO} "File sizes are $bsize and $asize blocks" >> ${outlog}

	if [ "x$shrink" = "x1" ]; then
		if [ $asize -ge $bsize ]; then
			rc=1
			${ECHO} "ERROR: File was expected to shrink"
		fi
	fi

	return $rc
}

# Tests

# Test "all zeroes"
test_all_zeroes()
{
	testfile=$1
	outlog=$2
	shrink=1
	maxcompact=0
	${DD} if=/dev/zero of=${testfile} bs=1M count=5 >>${outlog} 2>&1
	rc=$?
	if [ $rc -eq 0 ]; then
		punch_holes ${shrink} ${maxcompact} ${testfile} ${outlog}
		rc=$?
	fi
	return $rc
}

# Test "all data"
test_all_data()
{
	testfile=$1
	outlog=$2
	shrink=0
	maxcompact=0
	${DD} if=/dev/urandom of=${testfile} bs=1M count=5 >>${outlog} 2>&1
	rc=$?
	if [ $rc -eq 0 ]; then
		punch_holes ${shrink} ${maxcompact} ${testfile} ${outlog}
		rc=$?
	fi
	return $rc
}

# Test "zeroes then data"
test_zeroes_data()
{
	testfile=$1
	outlog=$2
	shrink=1
	maxcompact=0
	${DD} if=/dev/zero of=${testfile} bs=1M count=5 >>${outlog} 2>&1
	rc=$?
	if [ $rc -eq 0 ]; then
		${DD} if=/dev/urandom of=${testfile} bs=1M count=5 seek=5 >>${outlog} 2>&1
		rc=$?
	fi
	if [ $rc -eq 0 ]; then
		punch_holes ${shrink} ${maxcompact} ${testfile} ${outlog}
		rc=$?
	fi
	return $rc
}

# Test "data then zeroes"
test_data_zeroes()
{
	testfile=$1
	outlog=$2
	shrink=1
	maxcompact=0
	${DD} if=/dev/urandom of=${testfile} bs=1M count=5 >>${outlog} 2>&1
	rc=$?
	if [ $rc -eq 0 ]; then
		${DD} if=/dev/zero of=${testfile} bs=1M count=5 seek=5 >>${outlog} 2>&1
		rc=$?
	fi
	if [ $rc -eq 0 ]; then
		punch_holes ${shrink} ${maxcompact} ${testfile} ${outlog}
		rc=$?
	fi
	return $rc
}

# Test "hole then data"
test_hole_data()
{
	testfile=$1
	outlog=$2
	shrink=1
	maxcompact=0
	${DD} if=/dev/zero of=${testfile} bs=1M count=5 seek=5 >>${outlog} 2>&1
	rc=$?
	if [ $rc -eq 0 ]; then
		punch_holes ${shrink} ${maxcompact} ${testfile} ${outlog}
		rc=$?
	fi
	return $rc
}

# Test "Almost all zeroes"
test_almost_all_zeroes()
{
	testfile=$1
	outlog=$2
	shrink=1
	maxcompact=0
	${DD} if=/dev/zero of=${testfile} bs=1M count=5 >>${outlog} 2>&1
	rc=$?
	if [ $rc -eq 0 ]; then
		${DD} if=/dev/urandom of=${testfile} bs=1 count=10 seek=4090 conv=notrunc>>${outlog} 2>&1
		rc=$?
	fi
	if [ $rc -eq 0 ]; then
		punch_holes ${shrink} ${maxcompact} ${testfile} ${outlog}
		rc=$?
	fi
	return $rc
}

# Test "Mostly zeroes"
test_mostly_zeroes()
{
	testfile=$1
	outlog=$2
	shrink=1
	maxcompact=0

	${DD} if=/dev/zero of=${testfile} bs=1M count=5 >>${outlog} 2>&1
	rc=$?
	if [ $rc -eq 0 ]; then
		${DD} if=/dev/urandom of=${testfile} bs=1 count=10 seek=4090 conv=notrunc>>${outlog} 2>&1
		rc=$?
	fi
	if [ $rc -eq 0 ]; then
		${DD} if=/dev/urandom of=${testfile} bs=1 count=10 seek=2101244 conv=notrunc>>${outlog} 2>&1
		rc=$?
	fi
	if [ $rc -eq 0 ]; then
		${DD} if=/dev/urandom of=${testfile} bs=1 count=10 seek=4202490 conv=notrunc>>${outlog} 2>&1
		rc=$?
	fi
	if [ $rc -eq 0 ]; then
		punch_holes ${shrink} ${maxcompact} ${testfile} ${outlog}
		rc=$?
	fi
	return $rc
}

# Test "Data - Zero - Hole - Data - Hole - Zero - Data - Zero - Data"
test_max_compact()
{
	testfile=$1
	outlog=$2
	shrink=1
	maxcompact=1
	${DD} if=/dev/urandom of=${testfile} bs=1M count=1  conv=notrunc >>${outlog} 2>&1
	rc=$?
	if [ $rc -eq 0 ]; then
		${DD} if=/dev/zero of=${testfile} bs=1M count=1 seek=1  conv=notrunc >>${outlog} 2>&1
		rc=$?
	fi
	if [ $rc -eq 0 ]; then
		${DD} if=/dev/urandom of=${testfile} bs=1M count=1 seek=3  conv=notrunc >>${outlog} 2>&1
		rc=$?
	fi
	if [ $rc -eq 0 ]; then
		${DD} if=/dev/zero of=${testfile} bs=1M count=1 seek=5 conv=notrunc >>${outlog} 2>&1
		rc=$?
	fi
	if [ $rc -eq 0 ]; then
		${DD} if=/dev/urandom of=${testfile} bs=1M count=1 seek=6 conv=notrunc >>${outlog} 2>&1
		rc=$?
	fi
	if [ $rc -eq 0 ]; then
		${DD} if=/dev/zero of=${testfile} bs=1M count=1 seek=7 conv=notrunc >>${outlog} 2>&1
		rc=$?
	fi
	if [ $rc -eq 0 ]; then
		${DD} if=/dev/urandom of=${testfile} bs=1M count=1 seek=8 conv=notrunc >>${outlog} 2>&1
		rc=$?
	fi
	if [ $rc -eq 0 ]; then
		punch_holes ${shrink} ${maxcompact} ${testfile} ${outlog}
		rc=$?
	fi
	return $rc
}

# Test "random hole, data, zeroes"
test_random()
{
	testfile=$1
	outlog=$2
	shrink=1
	maxcompact=1

	for seek in $(${SEQ} 10000)
	do
		val=$[$RANDOM % 2]
		if [ $val -eq 0 ]; then
			${DD} if=/dev/zero of=${testfile} bs=4K count=1 seek=$seek conv=notrunc >>${outlog} 2>&1
			rc=$?
		elif [ $val -eq 1 ]; then
			${DD} if=/dev/urandom of=${testfile} bs=4K count=1 seek=$seek conv=notrunc>>${outlog} 2>&1
			rc=$?
		fi
		if [ $rc -ne 0 ]; then
			break;
		fi
		seek=$[$seek + 1]
	done
	if [ $rc -eq 0 ]; then
		punch_holes ${shrink} ${maxcompact} ${testfile} ${outlog}
		rc=$?
	fi
	return $rc
}

#
#
# MAIN
#
#

usage()
{
	${ECHO} "usage: ${APP} -p path-to-puncher -l logdir -d workdir"
	exit 1
}

while getopts "p:d:l:h?" args
do
	case "$args" in
		p) PUNCHER_BIN="$OPTARG";;
		d) WORKDIR="$OPTARG";;
		l) OUTDIR="$OPTARG";;
    		h) usage;;
    		?) usage;;
  	esac
done

if [ -z ${WORKDIR} ] ; then
	${ECHO} "ERROR: No workdir"
	usage
elif [ ! -d ${WORKDIR} ] ; then
	${ECHO} "ERROR: Invalid workdir ${WORKDIR}"
	exit 1
fi

if [ -z ${OUTDIR} ]; then
	${ECHO} "ERROR: No logdir"
	usage
elif [ ! -d ${OUTDIR} ] ; then
	${ECHO} "ERROR: Invalid logdir ${OUTDIR}"
	exit 1
fi

if [ ! -x ${PUNCHER_BIN} ]; then
	${ECHO} "ERROR: Invalid puncher utility ${PUNCHER_BIN}"
	exit 1
fi

RUNDATE=`${DATE} +%F_%H:%M`
LOGDIR=${OUTDIR}/${RUNDATE}
LOGFILE=${LOGDIR}/puncher_test.log

do_mkdir ${LOGDIR}

${ECHO} "Output log is ${LOGFILE}"

STARTRUN=$(date +%s)
log_message "*** Start puncher test ***"

FAIL=0
PASS=0
TESTNUM=1

for do_test in test_all_zeroes test_all_data test_zeroes_data \
	test_data_zeroes test_hole_data test_almost_all_zeroes \
	test_mostly_zeroes test_max_compact test_random
do
	TEST="Test ${TESTNUM}"
	OUTLOG=${LOGDIR}/puncher_test_${TESTNUM}.out
	TESTFILE=${WORKDIR}/test${TESTNUM}

	log_start "${TEST} $do_test"

	$do_test ${TESTFILE} ${OUTLOG}
	rc=$?

	log_end $rc "${TEST}"

	if [ $rc -eq 0 ]
	then
		PASS=$[$PASS + 1];
	else
		FAIL=$[$FAIL + 1];
	fi
	TESTNUM=$[${TESTNUM} + 1]
done

ENDRUN=$(date +%s)
DIFF=$(( ${ENDRUN} - ${STARTRUN} ))
log_message "Pass: $PASS  Fail: $FAIL"
log_message "Total Runtime ${DIFF} seconds"
