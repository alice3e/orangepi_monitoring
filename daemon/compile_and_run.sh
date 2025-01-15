#!/bin/bash

if [ ! -f ./my_monitord ]; then
	rm -rf my_monitord
fi

gcc -o my_monitord my_monitord.c -lcurl
sleep 1
sudo ./my_monitord
sleep 1
ps aux | grep my_monitord
sleep 0.2
#cat /var/log/mydaemon.log
# sudo pkill my_monitord