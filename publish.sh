#!/bin/sh
PACKAGE="spin"
VERSION="0.5"
FNAME="${PACKAGE}-${VERSION}.tar.gz"

scp /tmp/${FNAME} valibox.sidnlabs.nl:/var/www-valibox/downloads/src/spin/
