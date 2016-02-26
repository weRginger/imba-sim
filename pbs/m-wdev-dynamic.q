#PBS -N m-wdev-dynamic
#PBS -e m-wdev-dynamic.e
#PBS -o m-wdev-dynamic.o
#PBS -l walltime=24:00:00,nodes=1:ppn=6
#PBS -M ziqifan16@gmail.com
#PBS -m abe
#PBS -q lab
DIR=/home/dudh/ziqifan/imba-sim
TRACE=/home/dudh/ziqifan/traceFile/MSR-Cambridge
cd $DIR
./sim-ideal $TRACE/wdev_0.csv hybrid-dynamic-d1k-n1k.cfg hybrid-dynamic-d1k-n1k-wdev_0 &
./sim-ideal $TRACE/wdev_0.csv hybrid-dynamic-d2k-n2k.cfg hybrid-dynamic-d2k-n2k-wdev_0 &
./sim-ideal $TRACE/wdev_0.csv hybrid-dynamic-d4k-n4k.cfg hybrid-dynamic-d4k-n4k-wdev_0 &
./sim-ideal $TRACE/wdev_0.csv hybrid-dynamic-d8k-n8k.cfg hybrid-dynamic-d8k-n8k-wdev_0 &
./sim-ideal $TRACE/wdev_0.csv hybrid-dynamic-d16k-n16k.cfg hybrid-dynamic-d16k-n16k-wdev_0 &
./sim-ideal $TRACE/wdev_0.csv hybrid-dynamic-d32k-n32k.cfg hybrid-dynamic-d32k-n32k-wdev_0 &
wait


