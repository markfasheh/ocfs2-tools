#!/bin/bash

die() {
	echo $@", exiting" > /dev/stderr
	exit 1
}

usage() {
	echo "-r region		the hb region to watch"
}

while getopts ":r:" opt; do
        case $opt in
                r)
                        region="$OPTARG"
                        ;;
                \?) usage
        esac
done

[ -z "$region" ] && \
	die "a region must be specified with -r"

dev=$(cat $region/dev) || die "couldn't read $region/dev"
[ -z "$dev" ] && \
	die "$region/dev isn't set, is the region active?"

for attr in slot_bytes; do
	eval "$attr=$(cat $region/$attr)" || die "couldn't read $region/$attr"
done

watch -n 3 -d \
	"echo \"cat //heartbeat\" | debugfs.ocfs2 /dev/$dev 2>/dev/null \
	| od -A d -x | \
	awk '( NF > 2 ) { \$(1) = \$1 / $slot_bytes; if (\$1 == int(\$1)) print \$0 }'"
