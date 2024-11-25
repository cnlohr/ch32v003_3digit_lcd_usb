all : testtop

testtop : testtop.c
	gcc -o $@ $^ -I../../rv003usb/ch32v003fun/minichlink -ludev

clean :
	rm -rf *.o *~ testtop

