#!/bin/sh

PROJECT=ocfs2-tools

if (automake-1.9 --version) < /dev/null > /dev/null 2>&1; then
   ACLOCAL=aclocal-1.9
elif (automake-1.8 --version) < /dev/null > /dev/null 2>&1; then
   ACLOCAL=aclocal-1.8
elif (automake-1.7 --version) < /dev/null > /dev/null 2>&1; then
   ACLOCAL=aclocal-1.7
elif (automake-1.6 --version) < /dev/null > /dev/null 2>&1; then
   ACLOCAL=aclocal-1.6
else
    echo
    echo "  You must have automake 1.6 or newer installed to compile $PROJECT."
    echo "  Download the appropriate package for your distribution,"
    echo "  or get the source tarball at ftp://ftp.gnu.org/pub/gnu/automake/"
    echo
    exit 1
fi

rm -rf autom4te.cache
$ACLOCAL
autoconf
./configure "$@"
