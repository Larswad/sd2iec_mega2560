#!/bin/sh
/home/lars/arduino-1.6.3/hardware/tools/avr/bin/avrdude -C/home/lars/arduino-1.6.3/hardware/tools/avr/etc/avrdude.conf -patmega2560 -cwiring -P/dev/ttyACM0 -b115200 -D -Uflash:w:obj-m2560-arduino_mega2560/sd2iec.hex:i

