#!/bin/bash

# timestamp
ts=`date +%T`

if [ `pgrep -n httpserver` ] ; then
        if [ curl -s --head localhost:1337/ | grep "200 OK" > /dev/null ] ; then
		return 0
	else
		echo "$ts: Server was running but not responding --> restarting" >> check.log
		#ENTER RESTART CMD
else
	echo "$ts: Server was not running --> restarting" >> check.log"
	#ENTER RESTART CMD
