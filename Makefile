obj-m += hollyfs.o
# here are listed all the files to make
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
	gcc mkfs.c -g -o mkfs


first-time: all
	# first we are turning the swap partition off to use it as the pertition for our custom file system
	sudo swapoff -a
	# first we are executing the mkfs script to do the setup
	sudo ./mkfs
	# here the custom file system module is inserted
	sudo insmod hollyfs.ko
	# and this call mounts the file system that was just created on /dev/sda3
	sudo mount /dev/sda3 mount_pt/
