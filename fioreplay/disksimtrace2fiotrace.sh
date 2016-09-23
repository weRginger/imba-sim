#!/bin/bash

if [ $# -ne 3 ]
then
    echo Usage: $0 dev file_in file_out
fi

echo fio version 2 iolog > $3
echo $1 add >> $3
echo $1 open >> $3


cat $2 | awk -F" " -v dev=$1 '{ if ($5=="0") printf("%s write %.0f %.0f\n", dev, $3*4*1024, $4*4*1024); else printf("%s read %.0f %.0f\n", dev, $3*4*1024, $4*4*1024)};' >> $3

echo $1 close >> $3
