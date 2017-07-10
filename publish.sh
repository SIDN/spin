#!/bin/sh
PACKAGE="spin"
VERSION="0.4"
FNAME="${PACKAGE}-${VERSION}.tar.gz"

scp -4 /tmp/${FNAME} valibox.sidnlabs.nl:/var/www-valibox/downloads/src/spin/
