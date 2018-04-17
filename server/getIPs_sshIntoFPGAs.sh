#!/bin/bash

myIP=$(ifconfig | egrep -o '([0-9]{1,3}\.){3}[0-9]{3}')

for mm in $myIP; do
	tmp=$(echo $mm | cut -d . -f 4 | tr -d '[:space:]')
	if [ "$tmp" -lt 255 ]; then
		mip=$(echo $mm | cut -d . -f 4 | tr -d '[:space:]' )
		MYIP=$(echo $mm | tr -d '[:space:]' )
	fi
done
MYIP="192.168.1.102"
IPS=$(nmap -sP 192.168.1.0/24 | egrep -o '([0-9]{1,3}\.){3}[0-9]{3}')
for ipAddr in $IPS; do
	tmp=$(echo $ipAddr | cut -d . -f 4 | tr -d '[:space:]')
	LIP=$(echo $ipAddr | tr -d '[:space:]')
	if [ "$tmp" != "$mip" ]; then
		if [ "$tmp" -gt 111 ]; then
			#~ if [ "$tmp" -lt 131 ]; then
				#dummy=$(ssh-keygen -R $ipAddr)
	            #dummy=$(ssh-keyscan -H $ipAddr >> ~/.ssh/known_hosts)
	            echo "killall *HPS & sysctl -w net.ipv4.tcp_low_latency=0  & sysctl -w net.ipv4.tcp_slow_start_after_idle=0 & sysctl -w net.ipv4.tcp_congestion_control=cubic & sysctl -w net.ipv4.tcp_timestamp=1 & sysctl -w net.ipv4.tcp_sack=1 & exit" | sshpass -p 'terasic' ssh -tt "root@$ipAddr" > /dev/null
				sleep 0.1
				echo "nohup ./pollTestHPS 192.168.1.102 & exit" | sshpass -p 'terasic' ssh -tt "root@$ipAddr"
	            echo "into hpsIP: $tmp"
	            echo "into hpsIP: $ipAddr"
            #~ fi
		fi
	fi
done
