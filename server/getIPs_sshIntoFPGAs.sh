#!/bin/bash

# need to change enetDevice to whichever device has the 192.168.1.xxx IP
# on the computer in use
enetDevice="bond0"
myIP=$(ip addr show $enetDevice | egrep -o '([0-9]{1,3}\.){3}[0-9]{3}')

for mm in $myIP; do
	tmp=$(echo $mm | cut -d . -f 4 | tr -d '[:space:]')
	if [ "$tmp" -lt 250 ]; then
		mip=$(echo $mm | cut -d . -f 4 | tr -d '[:space:]' )
		MYIP=$(echo $mm | tr -d '[:space:]' )
	fi
done

IPS=$(nmap -sn 192.168.1.0/24 | egrep -o '([0-9]{1,3}\.){3}[0-9]{3}')
for ipAddr in $IPS; do
	tmp=$(echo $ipAddr | cut -d . -f 4 | tr -d '[:space:]')
	LIP=$(echo $ipAddr | tr -d '[:space:]')
	if [[ "$tmp" != "$mip" && "$tmp" -gt 110 && "$tmp" -lt 250 ]]; then

		echo "killall *HPS & sysctl -w net.ipv4.tcp_low_latency=0  & sysctl -w net.ipv4.tcp_slow_start_after_idle=0 & sysctl -w net.ipv4.tcp_congestion_control=cubic & sysctl -w net.ipv4.tcp_timestamp=1 & sysctl -w net.ipv4.tcp_sack=1 & exit" | sshpass -p 'terasic' ssh -tt "root@$ipAddr" > /dev/null
		sleep 0.1
		echo "nohup ./pollTestHPS $MYIP & exit" | sshpass -p 'terasic' ssh -tt "root@$ipAddr"
		echo "into hpsIP: $tmp"
		echo "into hpsIP: $ipAddr"

	fi

done
