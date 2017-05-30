#!/bin/bash

echo "checking if loaded already"
lsmod | grep nf_hw 2>&1 > /dev/null
if [ $? == 0 ]; then
  echo "Unloading module"
  sudo rmmod nf_hw
  echo "Module unloaded"
else
  echo "Module not loaded yet"
fi
echo "Loading module"
sudo insmod nf-hw.ko
echo "Module loaded"

