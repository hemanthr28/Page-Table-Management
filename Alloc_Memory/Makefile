obj-m += general_alloc.o

general_alloc: general_alloc.c
	$(CC) -o $@ $^

clean:
	rm -f general_alloc

.PHONY: all clean