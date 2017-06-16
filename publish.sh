#!/bin/sh
PACKAGE="spin"
VERSION="0.4"
FNAME="${PACKAGE}-${VERSION}.tar.gz"

scp -4 /tmp/${FNAME} tjeb.nl:/var/www/tjeb.nl/

