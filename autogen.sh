#!/bin/sh

PROJECT=ocfs2-tools

rm -rf autom4te.cache
autoconf
./configure "$@"
