
#
# This is the makefile for building test program for
# simulation bench and linux user-space HV CMD driver
#
#	hv_sim_tb : sample test program for simulation bench
#	hv_u_cmd : sample test program for linux user space HV CMD driver
#
# running hv_sim_tb perform a BSM 4KB read/write and a MMLS 4KB read/write
# running hv_u_cmd perform stress test additionally
#
# IMPORTANT : before running hv_sim_tb, see instruction on top of hv_dicovery.c
# 	      on how to set up required address space for simulation
# IMPORTANT : for hv_u_cmd, the program currently assumed the following address
#             and the space should be reserved using memmap=4G\$4G
#	      in /boot/grub2/grub.cfg file
#
#			HV DRAM : 0x100000000
#			HV MMIO : 0x180000000
#
SOURCE_SIM_TB = hv_cmd.c hv_mmio.c hv_discovery.c hv_test.c
SOURCE_U_CMD = hv_cmd.c hv_mmio.c hv_cmd_stress.c hv_discovery.c hv_test.c

default:
	cp ../hv_cmd.c .
	cp ../hv_cmd.h .
	cp ../hv_mmio.c .
	cp ../hv_mmio.h .
	cp ../hv_cmd_stress.c .
	gcc -g -DSIMULATION_TB $(SOURCE_SIM_TB) -o hv_sim_tb
	gcc -g -pthread -DUSER_SPACE_CMD_DRIVER -DSIMULATION_TB $(SOURCE_U_CMD) -o hv_u_cmd

clean:
	rm -f hv_sim_tb hv_u_cmd
	rm -f hv_cmd.c 
	rm -f hv_cmd.h 
	rm -f hv_mmio.c 
	rm -f hv_mmio.h
	rm -f hv_cmd_stress.c
	rm *~ *.o

	
