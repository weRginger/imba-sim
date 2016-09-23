SHELL:=/bin/bash

DEVICE = /dev/sdb
libzbc_bin = /home/fwu/libzbc/bench_fwu

# Offsets used by several tests.
LOW_OFFSET=17179869184 # start byte of the 64-th zone
LOW_OFFSET_4KOFF=17179873280 # 4KB offset of the 64-th zone
LOW_OFFSET_LAST_BLK=17448300544 # the last 4KB of the 64-th zone


MID_OFFSET=4000762036224 #start byte of the 14904-th zone
MID_OFFSET_4KOFF=4000762040320 # 4KB offset of the 14904-th zone
HIGH_OFFSET=8001255636992 # start byte of 29808-th zone
HIGH_OFFSET_4KOFF=8001255641088 # 4KB offset of the 29808-th zone

# Band size estimate and accuracy used by band size detection test.
ESTIMATE=52428800
ACCURACY=1048576

# Parameters used by the mapping type test.
#BAND_A_OFFSET=0
#BAND_B_OFFSET=52428800
#TRACK_SIZE=2097152
#BLOCK_SIZE=16384


BAND_A_OFFSET=17179869184 # start byte of the 64-th zone
BAND_B_OFFSET=19058917376 # start byte of the 71-th zone
#TRACK_SIZE=8388608 # first 8MB of a zone
#DOUBLE_TRACK_SIZE=16777216 # first 16MB of a zone

TRACK_SIZE=4194304 # first 4MB of a zone
DOUBLE_TRACK_SIZE=8388608 # first 8MB of a zone

#67108864 # first 64MB of data in a zone
BLOCK_SIZE=4096


TIMER_RES=30

FIRST_SMR_ZONE=64
