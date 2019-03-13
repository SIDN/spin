#!/bin/sh
PACKAGE=spin
VERSION=`cat VERSION`

OUTDIR="/tmp/spin_release_file/"
# remove it if it exists, hardcoded for protecting a bit against, say OUTDIR="/"
if [ -d "/tmp/spin_release_file/" ]; then
    rm -rf /tmp/spin_release_file/
fi

BNAME="${PACKAGE}-${VERSION}"

CHECK=1
OPTION=$1
if [ "$OPTION" = "-n" ]; then
    CHECK=0
fi

echo "cp -r * ${OUTDIR}${BNAME}/"
if [ $CHECK -eq 0 ]; then

    mkdir -p ${OUTDIR}${BNAME} &&\
    echo "Directory created" &&\
    cp -r * ${OUTDIR}${BNAME}/ &&\
    echo "Directory created" &&\
    (cd ${OUTDIR}${BNAME}/src; autoreconf --install && (make distclean||/bin/true) && rm -rf lua/tests && rm -rf src/tests) &&\
    echo "autoreconf called, creating tarball" &&\
    (cd ${OUTDIR}; tar -czvf ${BNAME}.tar.gz ${BNAME}) &&\
    echo "Created ${OUTDIR}${BNAME}.tar.gz" &&\
    #rm -rf ${OUTDIR}${BNAME}
    sha256sum ${OUTDIR}${BNAME}.tar.gz

else

    mkdir -p ${OUTDIR}${BNAME} &&\
    cp -r * ${OUTDIR}${BNAME}/ &&\
    (cd ${OUTDIR}${BNAME}/src/; autoreconf --install && ./configure && make && make distclean && rm -rf lua/tests && rm -rf src/tests) &&\
    (cd ${OUTDIR}; tar -czvf ${BNAME}.tar.gz ${BNAME}) &&\
    echo "Created ${OUTDIR}${BNAME}.tar.gz" &&\
    rm -rf ${OUTDIR}${BNAME} &&\
    sha256sum ${OUTDIR}${BNAME}.tar.gz

fi
