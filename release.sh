#!/bin/sh

APP='fhctrl'
VER='0.5'
DST='/media/bigpig/www/htdocs/kwintesencja'
PKG="${DST}/${APP}-${VER}.tar.xz"

rm -fr /tmp/${APP}-${VER}

cp -r . /tmp/${APP}-${VER}

echo "${PKG}"

tar cJv -C /tmp -f ${PKG} ${APP}-${VER}/
