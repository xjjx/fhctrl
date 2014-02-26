#!/bin/sh

APP='fhctrl'
VER='0.7.8'
DST='/bigpig/secure/XJ/sampler/fhctrl'
PKG="${DST}/${APP}-${VER}.tar.xz"

rm -fr /tmp/${APP}-${VER}

cp -r . /tmp/${APP}-${VER}

echo "${PKG}"

tar cJv -C /tmp --exclude-vcs -f ${PKG} ${APP}-${VER}/
