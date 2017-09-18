
obj-m := hv.o
hv-objs := 	hv_queue.o \
		hv_timer.o \
		hvdimm.o \
		hv_cmd.o \
		hv_cmd_test.o \
		hv_cmd_stress.o \
		hv_cdev.o \
		hv_params.o \
		hv_profile.o \
		hv_mmio.o \
		hv_cache.o

EXTRA_CFLAGS += -DDEBUG
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD)/discovery modules
	cp discovery/Module.symvers .
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	rm -rf *~ .*.cmd *.mod.* .tmp_versions *.o *.ko *.symvers *.order
	rm -rf discovery/*~ discovery/.*.cmd discovery/*.mod.* discovery/.tmp_versions discovery/*.o discovery/*.ko discovery/*.symvers discovery/*.order
	
