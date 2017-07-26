#!/bin/sh
PACKAGE=spin
VERSION=0.5

BNAME="${PACKAGE}-${VERSION}"

mkdir -p /tmp/${BNAME} &&\
cp -r * /tmp/${BNAME}/ &&\
(cd /tmp/${BNAME}; autoreconf --install && ./configure && make && make distclean && rm -rf lua/tests && rm -rf src/tests) &&\
(cd /tmp; tar -czvf ${BNAME}.tar.gz ${BNAME}) &&\
echo "Created /tmp/${BNAME}.tar.gz" &&\
rm -rf /tmp/${BNAME} &&\
md5sum /tmp/${BNAME}.tar.gz
