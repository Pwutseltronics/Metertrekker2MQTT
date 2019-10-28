DSMR/ESMR 5.0 telegram to MQTT pusher
=================================
Sketch to read data from the P1 port of DSMR compliant energy meters (used in the Netherlands).
It verifies the CRC16 of the telegram, greatly reducing the chance for errors.
The Rx from the P1 port is inverted using SoftwareSerial, so no hardware inversion is needed.
Built for and tested on Wemos D1 mini R2, will probably work on any ESP8266 derived device.

## How to use
Download the .ino and the CRC16 library file, open them in the Arduino IDE,
copy `settings.example.h` -> `settings.h`, adjust settings, upload to your Arduino.

To publish Influx lines to MQTT, the `MAX_PACKET_SIZE` constant in `PubSubClient.h` must be set to at least 1024.
You will have to edit this library file yourself for now, I will provide an integrated fix in the future.

So far it all seems to work well but I am not done with the initial development/testing yet.

## Dependencies
CRC16 library retrieved from [vinmenn/Crc16](https://github.com/vinmenn/Crc16) on Github

## Hardware
I have designed a PCB to handle the hardware-level interfacing with the P1 port,
contact me if you want one or check out my [Pwuts/Metertrekker](https://gitlab.com/Pwuts/Metertrekker) repo.
