obj-m	:=
MODULES :=

KDIR	:= /lib/modules/$(shell uname -r)/build
PWD		:= $(shell pwd)
KMISC   := /lib/modules/$(shell uname -r)/cpufrequtils/

ifeq ("$(CONFIG_ACPI_PROCESSOR)", "y")
  obj-m   += cpufreq-acpi_pdump.o 
  MODULES += cpufreq-acpi_pdump
endif

ifeq ("$(CONFIG_ACPI_PROCESSOR)", "m")
  obj-m   += cpufreq-acpi_pdump.o 
  MODULES := cpufreq-acpi_pdump
endif

ifeq ("$(CONFIG_X86_TSC)", "y")
  obj-m	 += cpufreq-test_tsc.o
  MODULES += cpufreq-test_tsc
endif

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	- rm -rf *.o *.ko .tmp-versions .*.cmd .*.mod.* *.mod.c
	- rm -rf .tmp_versions*

install: default
	install -d $(KMISC)
	install -m 644 -c $(addsuffix .ko,$(MODULES)) $(KMISC)
	/sbin/depmod -a

all:	default
