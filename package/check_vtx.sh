#!/bin/bash

if [ -n "$(cat /proc/cpuinfo | grep vmx)" ]; then
  echo "VTx is available"
else
  echo "VTx is not available"
fi

# End of this script
