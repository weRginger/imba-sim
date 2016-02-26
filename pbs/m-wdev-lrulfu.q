#PBS -N m-wdev-lrulfu
#PBS -e m-wdev-lrulfu.e
#PBS -o m-wdev-lrulfu.o
#PBS -l walltime=24:00:00,nodes=1:ppn=6
#PBS -M ziqifan16@gmail.com
#PBS -m abe
#PBS -q oc
DIR=/home/dudh/ziqifan/sim-ideal
TRACE=/home/dudh/ziqifan/traceFile/MSR-Cambridge
cd $DIR
./sim-ideal $TRACE/wdev_0.csv hybrid-lrulfu-d1k-n1k.cfg hybrid-lrulfu-d1k-n1k-wdev_0 &
./sim-ideal $TRACE/wdev_0.csv hybrid-lrulfu-d2k-n2k.cfg hybrid-lrulfu-d2k-n2k-wdev_0 &
./sim-ideal $TRACE/wdev_0.csv hybrid-lrulfu-d4k-n4k.cfg hybrid-lrulfu-d4k-n4k-wdev_0 &
./sim-ideal $TRACE/wdev_0.csv hybrid-lrulfu-d8k-n8k.cfg hybrid-lrulfu-d8k-n8k-wdev_0 &
./sim-ideal $TRACE/wdev_0.csv hybrid-lrulfu-d16k-n16k.cfg hybrid-lrulfu-d16k-n16k-wdev_0 &
./sim-ideal $TRACE/wdev_0.csv hybrid-lrulfu-d32k-n32k.cfg hybrid-lrulfu-d32k-n32k-wdev_0 &
wait


