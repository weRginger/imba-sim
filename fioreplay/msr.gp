reset

set terminal postscript eps color enhanced size 5,3.5 font "Times-Roman" 22
set output 'msr.eps'

set tics nomirror

#set xlabel 'U (%)'
#set ylabel 'Avg Band Cleaning Time (sec/band)'

#set lmargin at screen 0.1


set multiplot layout 3,1



set xrange [0:1000]
set ylabel "Latency (ms)"
plot 'proj_1_lat.1.log' u ($1/1000):($2/1000) notitle pt 0  w l

set ylabel "Throughput (MB/s)" 
plot 'proj_1_bw.1.log' u ($1/1000):($2/1000) notitle pt 0  w l

set ylabel "# non-seq zone"
plot 'nonseq_proj_1.csv' u 1:2 notitle  w lp pt 2




