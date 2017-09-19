# make with no parameters implies both BIO and SGIO
bio=1
ifeq ($(bio), 1)
EXTRA_CFLAGS += -DUSING_BIO
endif

sgio=0
ifeq ($(sgio), 1)
EXTRA_CFLAGS += -DUSING_SGIO
endif

# If both are undefined, compile both drivers by default
ifneq ($(bio), 1)
ifneq ($(sgio), 1)
EXTRA_CFLAGS += -DUSING_BIO -DUSING_SGIO
endif
endif

# Use DBG=1 to enable DEBUG capture, by default DEBUG is disabled
ifneq ($(DBG), 1)
EXTRA_CFLAGS += -DNO_STATS_DEBUG
endif

# Use use_tasklet=1 to enable using legacy tasklet operation instead of work queues for IO completions
# Note that kernels prior to 2.6.36 will always use tasklet mode regardless of this option.
ifneq ($(use_tasklet), 1)
EXTRA_CFLAGS += -DUSE_WORK_QUEUES
endif

# Use UNPLUG=1 to enable the unplug function per legacy operation by default it is disabled
ifeq ($(UNPLUG), 1)
EXTRA_CFLAGS += -DENABLE_UNPLUG
endif

# To disable compiling IOSTAT support altogether use IOSTAT=0
ifneq ($(IOSTAT), 0)
EXTRA_CFLAGS += -DCOMPILE_IOSTAT
endif

# This will bump the warning level to the same as Ubuntu drivers on non-Ubuntu systems
ifeq ($(UBUNTU_WARNING_LEVEL), 1)
EXTRA_CFLAGS += -Wformat -Wimplicit-function-declaration -Wformat-security -Wreturn-type
endif

obj-m := netlist_ev3.o

netlist_ev3-y    := netlist_ev3_main.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build

all : default
.PHONY : default

default:
	make -C $(KERNELDIR) SUBDIRS=$(CURDIR) modules 

clean:
	make -C $(KERNELDIR) SUBDIRS=$(CURDIR) clean

# make install requires root privileges - this is used during rpm install
install: netlist_ev3.ko
	mkdir -p /lib/modules/$(shell uname -r)/extra/netlist
	install -m 0755 netlist_ev3.ko /lib/modules/$(shell uname -r)/extra/netlist

.PHONY: install

