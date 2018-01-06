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
	$(CC) -O3 $(LDFLAGS)  $^ -o $@ 
%.o : %.c
	$(CC) -O3 $(CFLAGS) -c $< -o $@ 
	

.PHONY: clean
clean:
	rm -f $(TARGET) *.a *.o *~ 


# add this after the $@ in the build statements if you want to use the zip library -L/home/jonathan/Desktop/hpsOS/devVersion/client/zlibSource/final/lib -lz
