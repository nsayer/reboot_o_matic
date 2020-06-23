
# Change this to whatever AVR programmer you want to use.
PROGRAMMER = usbtiny

OUT=rebooter

# Change this if you're not using a Tiny85
#CHIP = attiny85
CHIP = attiny9

CC = avr-gcc
OBJCPY = avr-objcopy
AVRDUDE = avrdude

CFLAGS = -Os -g -mmcu=$(CHIP) -std=c11 -Wall -Wno-main -fno-tree-switch-conversion

DUDE_OPTS = -c $(PROGRAMMER) -p $(CHIP)

all:	$(OUT).hex

%.o:	%.c Makefile
	$(CC) $(CFLAGS) -c -o $@ $<

%.elf:	%.o
	$(CC) $(CFLAGS) -o $@ $^

%.hex:	%.elf
	$(OBJCPY) -j .text -j .data -O ihex $^ $@

clean:
	rm -f *.o *.elf *.hex

flash:	$(OUT).hex
	$(AVRDUDE) $(DUDE_OPTS) -U flash:w:$^

# This is only for the ATTinyx5
fuse:
	$(AVRDUDE) $(DUDE_OPTS) -U lfuse:w:0x62:m -U hfuse:w:0xdf:m -U efuse:w:0xff:m

init:	fuse flash

