#!/bin/sh
VERSIONFILE=$1
if [ -x ../.git ]; then
    BUILD_VERSION=`git describe --abbrev=8 --dirty --always --tags`
else
    BUILD_VERSION=`cat ${VERSIONFILE}`
fi
BUILD_DATE=`date`
echo "#include \"version.h\"" > version.c
echo "const char * BUILD_VERSION = \"${BUILD_VERSION}\";" >> version.c
echo "const char * BUILD_DATE = \"${BUILD_DATE}\";" >> version.c
