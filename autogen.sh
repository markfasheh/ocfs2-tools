#!/bin/sh

aclocal
autoconf
./configure "$@"
