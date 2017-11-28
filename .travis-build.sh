#!/bin/bash

# Run this script from a docker container:
# docker build -t spinbuild:latest .
# docker run -it --rm spinbuild

export KERNELPATH=$(/usr/bin/find /lib/modules -name build | head -n 1)

autoreconf --install
./configure
make -j

