DSMR/ESMR telegram to MQTT pusher
=================================
Sketch to read data from the P1 port of ESMR/DSMR enabled electricity meters.
It verifies the CRC16 of the telegram, greatly reducing the chance for errors.
In order for it to work, the Tx signal from the P1 port will have to be inverted
before feeding it to the Rx of the MCU.
Built for and tested on Wemos D1 mini R2, will probably work on any ESP8266 derived device.

## How to use
Download the .ino and the CRC16 library file, open them in the Arduino IDE,
set the MQTT server and topics, build an inverter for the TX of your P1 port.

## Dependencies
CRC16 library from [vinmenn/Crc16](https://github.com/vinmenn/Crc16) on Github