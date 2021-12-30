KERNEL_TREE_PATH?=/lib/modules/$(shell uname -r)/build

obj-m += page_map.o

all: page_map page_map.ko

page_map: page_map_user.c
	$(CC) -o $@ $^

page_map.ko: page_map.c
	make -C $(KERNEL_TREE_PATH) M=$(PWD) modules

clean:
	rm -f page_map
	make -C $(KERNEL_TREE_PATH) M=$(PWD) clean

.PHONY: all clean
