#!/bin/bash
mount -t vboxsf VM_Shared ~/Share
ndctl disable-namespace namespace0.0
ndctl destroy-namespace namespace0.0
ndctl create-namespace --mode=dax --region=0 --size=8g
daxctl reconfigure-device --mode=system-ram --no-online dax0.0
