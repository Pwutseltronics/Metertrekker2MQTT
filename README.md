DSMR/ESMR 5.0 telegram to MQTT pusher
=================================
Sketch to read data from the [P1 port] of DSMR compliant electricity meters.
It verifies the CRC16 of the telegram, greatly reducing the chance for errors.

[P1 port]: https://www.netbeheernederland.nl/_upload/Files/Slimme_meter_15_a727fce1f1.pdf

The Rx is an open collector output, which means that the current must be supplied
 by the receiving device and that the resulting output signal is inverted.
In the sketch, the Rx is inverted using SoftwareSerial, so no further
 hardware inversion is needed.

## Compatibility & Hardware
Written for and tested on Wemos D1 mini v2, should work on any ESP8266 derived device.

The RTS needs 5V input to be triggered. This means that if you use an ESP or other
 microcontroller that works on 3.3V, you will need a level shifter or an RTL inverter
 to control it.

I have designed a PCB to handle the hardware-level interfacing with the P1 port,
contact me if you want one or check out my [Pwuts/Metertrekker] repo.

[Pwuts/Metertrekker]: https://gitlab.com/Pwuts/Metertrekker

## How to use
Download the .ino and the CRC16 library file, open them in the Arduino IDE,
copy `settings.example.h` -> `settings.h`, adjust settings, upload to your device.

To publish Influx lines to MQTT, the `MAX_PACKET_SIZE` constant in `PubSubClient.h`
 must be set to at least 1024.
When using PlatformIO, *this will be taken care of by the included `platformio.ini`*,
 otherwise you must edit the library file.

## Dependencies
* CRC16 library retrieved from [vinmenn/Crc16] on Github
* [ESP-WiFiSettings] library

[vinmenn/Crc16]: https://github.com/vinmenn/Crc16
[ESP-WiFiSettings]: https://platformio.org/lib/show/7251/ESP-WiFiSettings

## Developing / contributing
If you want to work on, derive from, or tinker with this firmware or its functionality,
the following snippet may come in handy:
```C++
/* Valid telegram for testing purposes */
byte bufferIn[768] = "/ISK5\\2M550E-1012\r\n\r\n1-3:0.2.8(50)\r\n0-0:1.0.0(190827155511S)\r\n0-0:96.1.1(4D455445525F53455249414C235F484558)\r\n1-0:1.8.1(000057.460*kWh)\r\n1-0:1.8.2(000037.300*kWh)\r\n1-0:2.8.1(000000.000*kWh)\r\n1-0:2.8.2(000000.000*kWh)\r\n0-0:96.14.0(0002)\r\n1-0:1.7.0(00.498*kW)\r\n1-0:2.7.0(00.000*kW)\r\n0-0:96.7.21(00008)\r\n0-0:96.7.9(00002)\r\n1-0:99.97.0()\r\n1-0:32.32.0(00005)\r\n1-0:32.36.0(00001)\r\n0-0:96.13.0()\r\n1-0:32.7.0(235.4*V)\r\n1-0:31.7.0(002*A)\r\n1-0:21.7.0(00.454*kW)\r\n1-0:22.7.0(00.000*kW)\r\n0-1:24.1.0(003)\r\n0-1:96.1.0(4D455445525F53455249414C235F484558)\r\n0-1:24.2.1(190827155507S)(00004.380*m3)\r\n!";
int readLength = strlen((char*)bufferIn);
char receivedCRC[5] = "ECDF";
```
This telegram is ESMR 5.0 compliant and the given CRC16 is valid for this telegram.
