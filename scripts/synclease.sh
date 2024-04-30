#!/bin/sh

#Requested by meshagent to copy dnsmas.lease from ARM to ATOM 
#for the first time when mesh is enabled, also during plume agent
#restart

. /etc/device.properties

if [ -n "${ARM_INTERFACE_IP}" ]; then
    if [ ! -f /tmp/login.swr ]; then
        configparamgen jx /etc/dropbear/elxrretyt.swr /tmp/login.swr
    fi
    scp -i /tmp/login.swr root@${ARM_INTERFACE_IP}:/var/lib/misc/dnsmasq.leases /tmp/dnsmasq.leases
else
    tmpfile=$(mktemp /tmp/dnsmasq.leases.XXXXXX)
    flock /tmp/.dnsmasq_leases_lock cp /var/lib/misc/dnsmasq.leases $tmpfile
    mv $tmpfile /tmp/dnsmasq.leases
fi
