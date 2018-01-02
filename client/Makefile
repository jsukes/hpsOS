#
TARGET = devTestHPS

#
CROSS_COMPILE = arm-linux-gnueabihf-
CFLAGS = -static -g -Wall -I${SOCEDS_DEST_ROOT}/ip/altera/hps/altera_hps/hwlib/include
LDFLAGS =  -g -Wall
CC = $(CROSS_COMPILE)gcc
ARCH= arm


build: $(TARGET)
$(TARGET): HPSmain.o 
	$(CC) -O3 $(LDFLAGS)  $^ -o $@ -L/home/jonathan/Desktop/hpsOS/devVersion/client/zlibSource/final/lib -lz 
%.o : %.c
	$(CC) -O3 $(CFLAGS) -c $< -o $@ -L/home/jonathan/Desktop/hpsOS/devVersion/client/zlibSource/final/lib -lz

.PHONY: clean
clean:
	rm -f $(TARGET) *.a *.o *~ 
