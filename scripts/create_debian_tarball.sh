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

mkdir -p ${OUTDIR}${BNAME} &&\
cp -r . ${OUTDIR}${BNAME}/ &&\
cd ${OUTDIR}${BNAME} &&\
git clean -fxd &&\
echo "remove git files" &&\
find . -name .gitignore -exec rm {} \; &&\
rm -rf .git &&\
echo "create version" &&\
./scripts/create_version_c.sh VERSION &&\
echo "Remove test files" &&\
rm -rf src/tests lua/tests &&\
echo "remove all non-source files" &&\
rm -rf conntrack-tools scripts doc Changelog.md CONTRIBUTING.md LICENSE README.md VERSION &&\
mv src/* ./ &&\
rmdir src &&\
cd .. &&\
tar -czvf ${BNAME}.tar.gz ${BNAME} &&\
#rm -rf ${OUTDIR}${BNAME}
echo "Done."

