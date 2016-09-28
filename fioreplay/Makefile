Q_FLAG=-Q1 
W_FLAG=-W0
a_FLAG=-a0
A_FLAG=-A0

#TRACE=proj_1_pmem0-cache1
#TRACE=proj_1_sdb
TRACE=test

all:
	make run


run:
	#sudo fio mix.fio --read_iolog=$(TRACE).trace ;
	sudo fio mix.fio --read_iolog=$(TRACE).trace >> $(TRACE).bw;
	#		 --write_bw_log=$(TRACE) \
    	#		 --write_lat_log=$(TRACE) \
	#		 --write_iops_log=$(TRACE) ; \

#	trap "sudo kill $$!" EXIT

plot:
	gnuplot msr.gp
	evince msr.eps&

replot:
	gnuplot msr.gp


clean:
	rm  -f  *.log *.csv *.eps *.job
