#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "Crc16.h"
Crc16 CRC;

// WiFi and MQTT stuff
const char* ssid      = "NETWORK";
const char* password  = "WIFI_PASSWORD";
const char* mqttServ  = "MQTT_SERV_IP";
const int   mqttPort  = 1883;
const char* clientID  = "EnergyMonitor";

WiFiClient espClient;
PubSubClient client(espClient);
char msg[50];

// MQTT topic settings: empty topic will disable publishing 

#define ENERGY_DEL_LOW_TOPIC        "whiskeygrid/energy/mains/reading/delivered/low";
#define ENERGY_DEL_HIGH_TOPIC       "whiskeygrid/energy/mains/reading/delivered/high";
#define ENERGY_REDEL_LOW_TOPIC      "whiskeygrid/energy/mains/reading/redelivered/low";
#define ENERGY_REDEL_HIGH_TOPIC     "whiskeygrid/energy/mains/reading/redelivered/high";
#define TARIFF_TOPIC                "whiskeygrid/energy/mains/reading/tariff";
#define P_TOTAL_DEL_TOPIC           "whiskeygrid/energy/mains/power/total";
#define P_TOTAL_REDEL_TOPIC         "whiskeygrid/energy/mains/power/total_redelivery";
#define P_L1_TOPIC                  "whiskeygrid/energy/mains/power/L1"
#define P_L1_REDEL_TOPIC            "whiskeygrid/energy/mains/power/L1_redelivery"
#define V_L1_TOPIC                  "whiskeygrid/energy/mains/voltage/L1"
#define I_L1_TOPIC                  "whiskeygrid/energy/mains/current/L1"

#define GAS_READING_TOPIC           "whiskeygrid/energy/gas/reading";

#define VOLTAGE_SAGS_L1_TOPIC       "whiskeygrid/energy/mains/report/voltage_sags/L1"
#define VOLTAGE_SWELLS_L1_TOPIC     "whiskeygrid/energy/mains/report/voltage_swells/L1"
#define POWERFAILURES_TOPIC         "whiskeygrid/energy/mains/report/powerfailures";
#define POWERFAILURES_LONG_TOPIC    "whiskeygrid/energy/mains/report/powerfailures_long";
#define POWERFAILURE_DETAILS_TOPIC  "whiskeygrid/energy/mains/report/powerfailure_details";
#define TEXT_MESSAGE_TOPIC          "whiskeygrid/energy/mains/message";

const int interval = 15000;
int lastTelegram = -15000;
const int RTSpin = 5;

byte bufferIn[768];
int readLength;
char receivedCRC[5];

// byte bufferIn[768] = "/ISK5\\2M550E-1012\r\n\r\n1-3:0.2.8(50)\r\n0-0:1.0.0(190827155511S)\r\n0-0:96.1.1(4D455445525F53455249414C235F484558)\r\n1-0:1.8.1(000057.460*kWh)\r\n1-0:1.8.2(000037.300*kWh)\r\n1-0:2.8.1(000000.000*kWh)\r\n1-0:2.8.2(000000.000*kWh)\r\n0-0:96.14.0(0002)\r\n1-0:1.7.0(00.498*kW)\r\n1-0:2.7.0(00.000*kW)\r\n0-0:96.7.21(00008)\r\n0-0:96.7.9(00002)\r\n1-0:99.97.0()\r\n1-0:32.32.0(00005)\r\n1-0:32.36.0(00001)\r\n0-0:96.13.0()\r\n1-0:32.7.0(235.4*V)\r\n1-0:31.7.0(002*A)\r\n1-0:21.7.0(00.454*kW)\r\n1-0:22.7.0(00.000*kW)\r\n0-1:24.1.0(003)\r\n0-1:96.1.0(4D455445525F53455249414C235F484558)\r\n0-1:24.2.1(190827155507S)(00004.380*m3)\r\n!";
// int readLength = strlen((char*)bufferIn);
// char receivedCRC[5] = "ECDF";


void setup()
{
  Serial.begin(115200);
  Serial.setTimeout(100);
  pinMode(RTSpin, OUTPUT);
  digitalWrite(RTSpin, LOW);

  setupWifi();
  client.setServer(mqttServ, mqttPort);
  // client.setCallback(callback);
}

void loop()
{
  // WiFi and MQTT stuff
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (millis() - lastTelegram > interval) {
    if (Serial.available() > 0) {     // check for incoming serial data
      // while (Serial.available() > 0)  Serial.read();
      if (Serial.peek() == '/') {     // check for telegram header
        readLength = Serial.readBytesUntil('!', bufferIn, 766);
        bufferIn[readLength++] = '!';
        bufferIn[readLength] = 0;

        Serial.println("Telegram received!");

        Serial.print("telegram length: ");
        Serial.println(readLength);
        // Serial.println(bufferIn);

        Serial.readBytes(receivedCRC, 4);
        receivedCRC[4] = 0;
        Serial.print("read CRC: ");
        Serial.println(receivedCRC);

        if (verifyTelegram(bufferIn, receivedCRC)) {
          Serial.println("Telegram valid!");
          Serial.println();

          lastTelegram = millis();
          parseTelegram((char*)bufferIn);
        } else {
          Serial.println("Telegram NOT valid!");
          delay(200);
        }

        Serial.println();

      } else Serial.read();

    } else {
      requestTelegram();

      Serial.print("Waiting for telegram");
      for (int i = 0; i < 20; i++) {
        if (Serial.available() > 0)  break;

        Serial.print('.');
        delay(15);
      }
      Serial.print("\n\n");
    }
  } else if (Serial.available() > 0)  Serial.read(); // discard serial input
}


void requestTelegram()
{
  Serial.println("Requesting telegram...");
  digitalWrite(RTSpin, HIGH);
  delay(100);
  digitalWrite(RTSpin, LOW);
}


bool verifyTelegram(const byte* telegram, const char* checkCRC)
{
  char calculatedCRC[5] = "";

  sprintf(calculatedCRC, "%X", CRC.fastCrc((uint8_t*)telegram, 0, readLength, true, true, 0x8005, 0x0000, 0x0000, 0x8000, 0xffff));

  Serial.print("calculated CRC: ");
  Serial.println(calculatedCRC);

  return strncmp(calculatedCRC, checkCRC, 4) == 0;
}


String lastGasTimestamp = "", lastTextMessage = "";

void parseTelegram(char* telegram)
{
  int i = 0, valueStart;
  bool valueIsFloat = false;
  char *lineptr;
  byte hexbuf[3];
  String line, ident, value, tmpValue, description = "", topic = "", timestamp, gasTimestamp;
  line.reserve(64);
  ident.reserve(12);
  value.reserve(50);

  Serial.print("==== START OF TELEGRAM ====\n\n");

  lineptr = strtok(telegram, "\r\n");
  line = String(lineptr);

  Serial.print(line + "\n\n");

  // need to do anything with the device model header?

  while (lineptr = strtok(NULL, "\r\n")) {
    line = String(lineptr);

    if (line.charAt(0) == '!') {
      Serial.println("==== END OF TELEGRAM ====");
      break;
    }

    i++;
    Serial.print(i);
    Serial.print(": ");
    Serial.print(line);

    if (line.length() >= 8) {
      valueStart = line.lastIndexOf('(') + 1;

      if (valueStart != -1) {
        value = line.substring(valueStart, line.lastIndexOf(')'));


        ident = line.substring(0, line.indexOf('('));

        if (ident.equals("1-3:0.2.8")) {          // protocol version (should be 50)
          //TODO: use this value to adjust protocol handling
          topic = "";
          description = "SMR version";
          valueIsFloat = false;

        } else if (ident.equals("0-0:1.0.0")) {   // timestamp
          topic = description = "";
          valueIsFloat = false;

          timestamp = value.substring(0, value.length() - 1);

        } else if (ident.equals("0-0:96.1.1")) {  // serial number (ascii hex) - is this useful?
          topic = "";
          description = "serial number";
          valueIsFloat = false;

          tmpValue = "";
          tmpValue.reserve(value.length()/2);

          for (int i = 0; i < value.length()/2; i++) {
            value.substring(i*2).getBytes(hexbuf, 3);
            hexbuf[2] = 0;

            tmpValue.concat((char)strtol((char*)hexbuf, NULL, 16));
          }
          value = tmpValue;

        } else if (ident.equals("1-0:1.8.1")) {   // total delivered energy (low tariff)
          topic = ENERGY_DEL_LOW_TOPIC;
          description = "total delivered energy (low tariff)";
          valueIsFloat = true;

        } else if (ident.equals("1-0:1.8.2")) {   // total delivered energy (high tariff)
          topic = ENERGY_DEL_HIGH_TOPIC;
          description = "total delivered energy (high tariff)";
          valueIsFloat = true;

        } else if (ident.equals("1-0:2.8.1")) {   // total redelivery (low tariff)
          topic = ENERGY_REDEL_LOW_TOPIC;
          description = "total redelivered (low tariff)";
          valueIsFloat = true;

        } else if (ident.equals("1-0:2.8.2")) {   // total redelivery (high tariff)
          topic = ENERGY_REDEL_HIGH_TOPIC;
          description = "total redelivered (high tariff)";
          valueIsFloat = true;

        } else if (ident.equals("0-0:96.14.0")) { // tariff indicator
          topic = TARIFF_TOPIC;
          description = "tariff";
          valueIsFloat = false;

        } else if (ident.equals("1-0:1.7.0")) {   // actual total power
          topic = P_TOTAL_DEL_TOPIC;
          description = "total power";
          valueIsFloat = true;

        } else if (ident.equals("1-0:2.7.0")) {   // actual total redelivery power
          topic = P_TOTAL_REDEL_TOPIC;
          description = "total redelivery power";
          valueIsFloat = true;

        } else if (ident.equals("0-0:96.7.21")) { // # of power failures
          topic = POWERFAILURES_TOPIC;
          description = "power failures";
          valueIsFloat = false;

        } else if (ident.equals("0-0:96.7.9")) {  // # of long power failures
          topic = POWERFAILURES_LONG_TOPIC;
          description = "long power failures";
          valueIsFloat = false;

        } else if (ident.equals("1-0:99.97.0")) { // power failure event log (time + duration)
          topic = POWERFAILURE_DETAILS_TOPIC;
          description = "power failure event log";

        } else if (ident.equals("1-0:32.32.0")) { // # of voltage sags in L1
          topic = VOLTAGE_SAGS_L1_TOPIC;
          description = "L1 voltage sags";
          valueIsFloat = false;

        } else if (ident.equals("1-0:32.36.0")) { // # of voltage swells in L1
          topic = VOLTAGE_SWELLS_L1_TOPIC;
          description = "L1 voltage swells";
          valueIsFloat = false;

        } else if (ident.equals("0-0:96.13.0")) { // text message
          topic = TEXT_MESSAGE_TOPIC;
          description = "text message";
          valueIsFloat = false;

        } else if (ident.equals("1-0:32.7.0")) {  // actual L1 voltage
          topic = V_L1_TOPIC;
          description = "L1 voltage";
          valueIsFloat = true;

        } else if (ident.equals("1-0:31.7.0")) {  // actual L1 current
          topic = I_L1_TOPIC;
          description = "L1 current";
          valueIsFloat = true;

        } else if (ident.equals("1-0:21.7.0")) {  // actual L1 power
          topic = P_L1_TOPIC;
          description = "L1 power";
          valueIsFloat = true;

        } else if (ident.equals("1-0:22.7.0")) {  // actual L1 redelivery power
          topic = P_L1_REDEL_TOPIC;
          description = "L1 redelivery power";
          valueIsFloat = true;

        } else if (ident.equals("0-1:24.1.0")) {  // gas meter device type
          topic = "";
          description = "gas meter device type";
          valueIsFloat = false;

        } else if (ident.equals("0-1:96.1.0")) {  // gas meter serial number (ascii hex)
          topic = "";
          description = "gas meter SN";
          valueIsFloat = false;

          tmpValue = "";
          tmpValue.reserve(value.length()/2);

          for (int i = 0; i < value.length()/2; i++) {
            value.substring(i*2).getBytes(hexbuf, 3);
            hexbuf[2] = 0;

            tmpValue.concat((char)strtol((char*)hexbuf, NULL, 16));
          }
          value = tmpValue;

        } else if (ident.equals("0-1:24.2.1")) {  // gas meter last reading (timestamp + reading)
          description = "gas meter last reading";
          valueIsFloat = true;

          gasTimestamp = line.substring(line.indexOf('(') + 1, line.indexOf(')') - 1);
          Serial.print("\t@{" + gasTimestamp + "}");

          if (gasTimestamp > lastGasTimestamp) {
            topic = GAS_READING_TOPIC;
          } else {
            topic = "";
          }

          lastGasTimestamp = gasTimestamp;

        } else {  // ident not known
          topic = description = "";
          valueIsFloat = false;
        }

        if (valueIsFloat)  value.replace("*", " ");

        if (description.length() > 0) {
          Serial.print("  -> " + description + " [" + value + "]");
        }

        if (topic.length() > 0) {
          client.publish(topic.c_str(), value.c_str(), true);
        }
      }
    }
    Serial.print("\n\n");
  }

}

void setupWifi()
{
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect()
{
  int connLoseMillis = millis();
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");

    if (client.connect(clientID)) { // Attempt to connect
      Serial.println("connected");

      // Once connected, publish an announcement...
      int connectMillis = millis();
      float lostSeconds = (connectMillis - connLoseMillis) / 1000;
      char lostSecStr[6];
      dtostrf(lostSeconds, 1, 1, lostSecStr);
      sprintf(msg, "%s (re)connected after %ss", clientID, lostSecStr);
      // client.publish(connTopic, msg);
      Serial.println(msg);

      // client.subscribe(callTopic);  // ... and resubscribe
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");

      delay(5000);  // Wait 5 seconds before retrying
    }
  }
}
