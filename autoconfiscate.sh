#!/bin/sh
aclocal --force &&
autoheader --force &&
libtoolize --force && 
automake -af &&
autoconf --force &&
#if test -d test; then cd test && ./autoconfiscate.sh && cd ..; fi
if test -f mofc/autoconfiscate.sh; then cd mofc && ./autoconfiscate.sh; fi
if test -f cmpi-devel/autoconfiscate.sh; then cd cmpi-devel && ./autoconfiscate.sh; fi
