#!/bin/bash

exec >>/var/log/wiipresent.log 2>&1

killall -KILL wiipresent

while true; do
        date
        wiipresent -d :0.0 -l 45 -r -t -v -v
        echo "=============="
done
