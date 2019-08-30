DSMR/ESMR 5.0 telegram to MQTT pusher
=================================
Sketch to read data from the P1 port of SMR 5.0 enabled electricity meters (used in the Netherlands).
It verifies the CRC16 of the telegram, greatly reducing the chance for errors.
In order for it to work, the Tx signal from the P1 port will have to be inverted
before feeding it to the Rx of the MCU.
Built for and tested on Wemos D1 mini R2, will probably work on any ESP8266 derived device.

## How to use
~~Download the .ino and the CRC16 library file, open them in the Arduino IDE,
set the MQTT server and topics, build an inverter for the Tx of your meters P1 port.~~

The sketch is currently in active development and has been modified (commenting here, writing some code there)
to enable testing with a hardcoded telegram. *It will not work as is at the moment!!*
So far it all seems to work well but I am not done with the initial development/testing yet.

## Dependencies
CRC16 library retrieved from [vinmenn/Crc16](https://github.com/vinmenn/Crc16) on Github