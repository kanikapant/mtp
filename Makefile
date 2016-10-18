# Makefile for compiling kernel module for KVM-TMEM-BACKEND (partial implementation)
# After making use this cmd to 
#		Insert module: sudo insmod ./ktb.ko
#		Remove module: sudo rmmod ktb.ko

obj-m += tmem_bknd1.o

ktb-objs := tmem_bknd1.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean