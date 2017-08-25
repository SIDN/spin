#!/bin/sh
PACKAGE="spin"
VERSION="0.5"
FNAME="${PACKAGE}-${VERSION}.tar.gz"

scp -P 1234 /tmp/${FNAME} 127.0.0.1:/var/www-valibox/downloads/src/spin/
