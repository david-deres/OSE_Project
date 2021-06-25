#!/bin/bash

tab="--tab"
cmd="bash -c 'make nc-194';bash"
foo=""

for i in 1 2 ... 5; do
      foo+=($tab -e "$cmd")         
done

gnome-terminal --command="bash -c 'make run-chatsrv; $SHELL'"

sleep 5

gnome-terminal "${foo[@]}"

exit 0
