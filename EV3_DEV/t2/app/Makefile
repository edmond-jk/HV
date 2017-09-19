# Netlist Copyright 2015, All Rights Reserved
# ExpressVault Applications

# These are the default installation directories. Allow override at the command-line
bindir = "/usr/bin"
exec_prefix = "/usr"

# This will bump the warning level to the same as Ubuntu drivers on non-Ubuntu systems
ifeq ($(UBUNTU_WARNING_LEVEL), 1)
EXTRA_CFLAGS += -Wformat -Wimplicit-function-declaration -Wformat-security -Wreturn-type
endif

ev3util: ev3util.o ev3_json.o ev3_json_cmd.o
	cc -o ev3util ev3util.o ev3_json.o ev3_json_cmd.o

ev3util.o: ../netlist_ev3_ioctl.h ev3util.h ev3util.c ev3_json.h
	cc $(EXTRA_CFLAGS) -o ev3util.o -c ev3util.c

ev3_json.o: ev3_json.h ev3_json.c
	cc $(EXTRA_CFLAGS) -o ev3_json.o -c ev3_json.c

ev3_json_cmd.o: ev3_json.h ev3_json_cmd.c
	cc $(EXTRA_CFLAGS) -o ev3_json_cmd.o -c ev3_json_cmd.c

clean:
	rm -f ev3util.o
	rm -f ev3util

# make install requires root privileges - this is used during rpm install
install: ev3util
	mkdir -p $(bindir)
	mkdir -p $(exec_prefix)/local/bin/netlist
	install -m 0755 ev3util $(bindir)
	install -m 0755 ev3load $(bindir)
	install -m 0755 ../tools/*.sh $(exec_prefix)/local/bin/netlist

.PHONY: install
	
