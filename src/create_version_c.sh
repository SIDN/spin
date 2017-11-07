#!/bin/sh
GIT_COMMIT_STR=`git describe --abbrev=8 --dirty --always --tags`
BUILD_DATE_STR=`date`
echo "#include \"version.h\"" > version.c
echo "const char * VERSION_GIT_COMMIT = \"${GIT_COMMIT_STR}\";" >> version.c
echo "const char * VERSION_BUILD_DATE = \"${BUILD_DATE_STR}\";" >> version.c
