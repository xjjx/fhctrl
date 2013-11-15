#!/bin/sh

APP='nxjsm'
VER=$(sed -ne 's/^#define VERSION."\([0-9].*\)"/\1/p' config.h)
DST='/tmp'
PKG="${DST}/${APP}-${VER}.tar.xz"
TMPDIR="/tmp/${APP}-${VER}"

rm -fr ${TMPDIR}
cp -r . ${TMPDIR}

echo "${PKG}"

tar cJv -C /tmp --exclude-vcs -f ${PKG} ${TMPDIR}/
