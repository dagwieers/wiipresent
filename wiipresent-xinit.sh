#!/bin/bash

(
    killall wiipresent.sh wiipresent

    while true; do
        date
        wiipresent -l 45 -r -t -v -v
        echo "=============="
    done
) >>/var/log/wiipresent.log 2>&1 &
