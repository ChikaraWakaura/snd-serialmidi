KERNEL_HEADERS=/lib/modules/$(shell uname -r)/build
KERNEL_BASE=/lib/modules/$(shell uname -r)/kernel

snd-serialmidi-objs := serialmidi.o

obj-m := snd-serialmidi.o

all:
	$(MAKE) -C $(KERNEL_HEADERS) M=$(shell pwd) modules

clean:
	$(MAKE) -C $(KERNEL_HEADERS) M=$(shell pwd) clean 

install:
	cp snd-serialmidi.ko $(KERNEL_BASE)/sound/drivers
	depmod -a
	modprobe snd-serialmidi sdev=/dev/midi devices=2

uninstall:
	modprobe -r snd-serialmidi
	rm $(KERNEL_BASE)/sound/drivers/snd-serialmidi.ko
	depmod -a
