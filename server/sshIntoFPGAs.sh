#!/bin/bash

myIP=$(ifconfig | egrep -o '([0-9]{1,3}\.){3}[0-9]{3}')

for mm in $myIP; do
	tmp=$(echo $mm | cut -d . -f 4 | tr -d '[:space:]')
	if (( $tmp < 255 )); then
		mip=$(echo $mm | cut -d . -f 4 | tr -d '[:space:]' )
		MYIP=$(echo $mm | tr -d '[:space:]' )
	fi
done

filename="ipList"

while read -r line
do
    ipAddr="$line"
	echo "killall *HPS & exit" | sshpass -p 'terasic' ssh -t -t "root@$ipAddr" > /dev/null
	sleep 0.2
	echo "nohup ./newTestHPS $MYIP & exit" | sshpass -p 'terasic' ssh -t -t "root@$ipAddr"
	echo "into hpsIP: $ipAddr"
done < "$filename"
