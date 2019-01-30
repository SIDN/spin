#!/bin/sh
PACKAGE=spin
VERSION=`cat VERSION`

OUTDIR="/tmp/spin_release_file/"
BNAME="${PACKAGE}-${VERSION}"

CHECK=1
OPTION=$1
if [ "$OPTION" = "-n" ]; then
    CHECK=0
fi

if [ $CHECK -eq 0 ]; then

    mkdir -p ${OUTDIR}${BNAME} &&\
    cp -r * ${OUTDIR}${BNAME}/ &&\
    (cd ${OUTDIR}${BNAME}/src; autoreconf --install && (make distclean||/bin/true) && rm -rf lua/tests && rm -rf src/tests) &&\
    (cd ${OUTDIR} tar -czvf ${BNAME}.tar.gz ${BNAME}) &&\
    echo "Created ${OUTDIR}${BNAME}.tar.gz" &&\
    rm -rf ${OUTDIR}${BNAME}
    sha256sum ${OUTDIR}${BNAME}.tar.gz

else

    mkdir -p ${OUTDIR}${BNAME} &&\
    cp -r * ${OUTDIR}${BNAME}/ &&\
    (cd ${OUTDIR}${BNAME}/src/; autoreconf --install && ./configure && make && make distclean && rm -rf lua/tests && rm -rf src/tests) &&\
    (cd ${OUTDIR} tar -czvf ${BNAME}.tar.gz ${BNAME}) &&\
    echo "Created ${OUTDIR}${BNAME}.tar.gz" &&\
    rm -rf ${OUTDIR}${BNAME} &&\
    sha256sum ${OUTDIR}${BNAME}.tar.gz

fi
