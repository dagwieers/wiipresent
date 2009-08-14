#!/bin/bash

if ! hcitool dev | grep -q hci0; then
    echo "Cannot find any local bluetooth device. Bailing out." >&2
    exit 1
fi

while true; do
    SYSTEM="$(awk '/^host[0-9]$/ { print $1 }' /proc/cmdline)"
    case $SYSTEM in
        (host1) OPTIONS="-b 00:11:22:33:44:55 -b 00:11:22:33:44:55";;
        (host2) OPTIONS="-b 00:11:22:33:44:55 -b 00:11:22:33:44:55";;
        (host3) OPTIONS="-b 00:11:22:33:44:55 -b 00:11:22:33:44:55";;
        (host4) OPTIONS="-b 00:11:22:33:44:55 -b 00:11:22:33:44:55";;
        (host5) OPTIONS="-b 00:11:22:33:44:55 -b 00:11:22:33:44:55";;
        (host6) OPTIONS="-b 00:11:22:33:44:55 -b 00:11:22:33:44:55";;
        (*) OPTIONS=""
    esac
    wiipresent $OPTIONS -l 45 -r -t
done
