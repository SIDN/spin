#!/bin/bash

echo "checking if loaded already"
lsmod | grep spin 2>&1 > /dev/null
if [ $? == 0 ]; then
  echo "Unloading module"
  sudo rmmod spin
  echo "Module unloaded"
else
  echo "Module not loaded yet"
fi
echo "Loading module"
sudo insmod spin.ko mode=local verbosity=5
echo "Module loaded"

