#!/bin/sh
PACKAGE=spin
VERSION=0.2

BNAME="${PACKAGE}-${VERSION}"

autoreconf --install &&\
./configure &&\
make &&\
make distclean &&\
mkdir -p /tmp/${BNAME} &&\
cp -r * /tmp/${BNAME}/ &&\
(cd /tmp; tar -czvf ${BNAME}.tar.gz ${BNAME}) &&\
echo "Created /tmp/${BNAME}.tar.gz" &&\
rm -rf /tmp/${BNAME} &&\
md5sum /tmp/${BNAME}.tar.gz
