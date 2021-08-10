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

mkdir -p ${OUTDIR}${BNAME} &&\
cp -r . ${OUTDIR}${BNAME}/ &&\
(cd ${OUTDIR}${BNAME}; git clean -fxd) &&\
(cd ${OUTDIR}${BNAME}; autoreconf --install && ./configure && rm -rf lua/tests && rm -rf tests) &&\
(cd ${OUTDIR}${BNAME} && rm -rf .git .gitignore && rm -rf build) &&\
if [ $CHECK -eq 1 ]; then
    (cd ${OUTDIR}${BNAME}/; make && make distclean)
fi &&\
(cd ${OUTDIR}; tar -czvf ${BNAME}.tar.gz ${BNAME}) &&\
echo "Created ${OUTDIR}${BNAME}.tar.gz" &&\
rm -rf ${OUTDIR}${BNAME} &&\
sha256sum ${OUTDIR}${BNAME}.tar.gz
