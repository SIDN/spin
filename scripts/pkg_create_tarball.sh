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
(cd ${OUTDIR}${BNAME}/src; autoreconf --install && ./configure && rm -rf lua/tests && rm -rf tests) &&\
if [ $CHECK -eq 1 ]; then
    (cd ${OUTDIR}${BNAME}/src; make && make distclean)
fi &&\
(cd ${OUTDIR}${BNAME} && rm -rf .git .gitignore && rm -rf build) &&\
(cd ${OUTDIR}${BNAME}/src && rm -f spinweb/static/spin_graph/js/jquery-3.1.1.min.js.gz) &&\
(cd ${OUTDIR}${BNAME}/src && rm -rf spinweb/static/spin_graph/js/jquery-ui-1.12.1.custom) &&\
(cd ${OUTDIR}${BNAME}/src && rm -f lib/version.c) &&\
(cd ${OUTDIR}; tar -czvf ${BNAME}.tar.gz ${BNAME}) &&\
echo "Created ${OUTDIR}${BNAME}.tar.gz" &&\
rm -rf ${OUTDIR}${BNAME} &&\
sha256sum ${OUTDIR}${BNAME}.tar.gz
