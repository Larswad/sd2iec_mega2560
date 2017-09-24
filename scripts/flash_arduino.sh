#!/bin/sh

AVRDUDE=`which avrdude`
# An alternate path to the avrdude executable could be something on the line below if you have the arduino environment installed somewhere.
#/home/$USER/arduino-1.6.3/hardware/tools/avr/bin/avrdude

CONFPATH=/etc/avrdude.conf
# An alternate path to the conf file could be something on the line below if you have the arduino environment installed somewhere.
#/home/$USER/arduino-1.6.3/hardware/tools/avr/etc/avrdude.conf

PORT=ttyACM0
HEXPATH=obj-m2560-arduino_mega2560/sd2iec.hex

$AVRDUDE -C $CONFPATH -patmega2560 -cwiring -P/dev/$PORT -b115200 -D -Uflash:w:$HEXPATH:i

