#!/bin/bash

die() {
	echo $@", exiting" > /dev/stderr
	exit 1
}

usage() {
	echo "-r region		the hb region to watch"
	echo "-d device		the hb device to watch"
}

while getopts ":r:d:" opt; do
        case $opt in
                r)
                        region="$OPTARG"
			dev=$(cat $region/dev) || die "couldn't read $region/dev"
			[ -z "$dev" ] && \
				die "$region/dev isn't set, is the region active?"

			eval "$device=/dev/$dev"

			eval "slot_bytes=$(cat $region/$attr)" || die "couldn't read $region/$attr"
                        ;;
		d)
			dev="$OPTARG"
			eval "device=$dev"
			eval "slot_bytes=512"
			;;
                \?) usage
        esac
done

[ -z "$device" ] && \
	die "a region must be specified with -r or a device with -d"

watch -n 3 -d \
	"echo \"cat //heartbeat\" | debugfs.ocfs2 -n $device 2>/dev/null \
	| od -A d -x | \
	awk '( NF > 2 ) { \$(1) = \$1 / $slot_bytes; if (\$1 == int(\$1)) print \$0 }'"
