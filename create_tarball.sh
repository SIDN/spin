#!/bin/sh
PACKAGE=spin
VERSION=`cat VERSION`

BNAME="${PACKAGE}-${VERSION}"

CHECK=1
OPTION=$1
if [ "$OPTION" = "-n" ]; then
    CHECK=0
fi

if [ $CHECK -eq 0 ]; then

    mkdir -p /tmp/${BNAME} &&\
    cp -r * /tmp/${BNAME}/ &&\
    (cd /tmp/${BNAME}; autoreconf --install && (make distclean||/bin/true)) &&\
    (cd /tmp; tar -czvf ${BNAME}.tar.gz ${BNAME}) &&\
    echo "Created /tmp/${BNAME}.tar.gz" &&\
    rm -rf /tmp/${BNAME}
    sha256sum /tmp/${BNAME}.tar.gz

else

    mkdir -p /tmp/${BNAME} &&\
    cp -r * /tmp/${BNAME}/ &&\
    (cd /tmp/${BNAME}; autoreconf --install && ./configure && make && make distclean && rm -rf lua/tests && rm -rf src/tests) &&\
    (cd /tmp; tar -czvf ${BNAME}.tar.gz ${BNAME}) &&\
    echo "Created /tmp/${BNAME}.tar.gz" &&\
    rm -rf /tmp/${BNAME} &&\
    sha256sum /tmp/${BNAME}.tar.gz

fi
