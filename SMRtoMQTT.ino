#include <ESP8266WiFi.h>
#include <PubSubClient.h>
// to use PubSubClient with influx publishing enabled, the MAX_PACKET_SIZE constant in PubSubClient.h must be set to at least 1024 (to be sure)

#include "Crc16.h"
Crc16 CRC;

// WiFi and MQTT stuff
const char* ssid      = "NETWORK";
const char* password  = "WIFI_PASSWORD";
const char* mqttServ  = "MQTT_SERV_IP";
const int   mqttPort  = 1883;
const char* clientID  = "EnergyMonitor";
const char* connTopic = "whiskeygrid/debug/node_connect";

#define INFLUX  // comment to disable influx
#ifdef INFLUX
  const char* influxTopic = "whiskeygrid/energy/influx";
  const char* influxMeasurement = "energy";
#endif

typedef struct {
  char ident[12];
  int type;
  char influx_column[24];
  char topic[72];
  char description[48];
} metricDef;

#define METRIC_TYPE_META      1
#define METRIC_TYPE_META_TEXT 2
#define METRIC_TYPE_FLOAT    11
#define METRIC_TYPE_TEXT     12
#define METRIC_TYPE_BARE     13
#define METRIC_TYPE_GAS      21
#define METRIC_TYPE_OTHER    99

// Metric and MQTT topic settings: comment a line to disable processing/publishing of the metric
// leaving the third entry (influx_column) empty will disable inclusion in the influx formatted metric, if enabled

metricDef metricDefs[] = {
//{ "OBIS ref.",    METRIC_TYPE_CONSTANT,   "influx col.",    "MQTT topic", "description (only for serial debug output)" },
  { "1-3:0.2.8",    METRIC_TYPE_META,       "",               "",           "SMR protocol version"  },
  { "0-0:1.0.0",    METRIC_TYPE_BARE,       "timestamp",      "",           "telegram timestamp"    },
  { "0-0:96.1.1",   METRIC_TYPE_META_TEXT,  "meter_sn",       "",           "meter serial number"   },

  { "1-0:1.8.1",    METRIC_TYPE_FLOAT,  "delivered_low",      "whiskeygrid/energy/mains/reading/delivered/low",     "total delivered energy (low tariff)" },
  { "1-0:1.8.2",    METRIC_TYPE_FLOAT,  "delivered_high",     "whiskeygrid/energy/mains/reading/delivered/high",    "total delivered energy (high tariff)" },
  { "1-0:2.8.1",    METRIC_TYPE_FLOAT,  "redelivered_low",    "whiskeygrid/energy/mains/reading/redelivered/low",   "total redelivered (low tariff)" },
  { "1-0:2.8.2",    METRIC_TYPE_FLOAT,  "redelivered_high",   "whiskeygrid/energy/mains/reading/redelivered/high",  "total redelivered (high tariff)" },
  { "0-0:96.14.0",  METRIC_TYPE_BARE,   "tariff",             "whiskeygrid/energy/mains/reading/tariff",            "tariff" },
  { "1-0:1.7.0",    METRIC_TYPE_FLOAT,  "P_total",            "whiskeygrid/energy/mains/power/total",               "total power" },
  { "1-0:2.7.0",    METRIC_TYPE_FLOAT,  "P_total_redelivery", "whiskeygrid/energy/mains/power/total_redelivery",    "total redelivery power" },
  { "1-0:21.7.0",   METRIC_TYPE_FLOAT,  "P_L1",               "whiskeygrid/energy/mains/power/L1",                  "L1 power" },
  { "1-0:22.7.0",   METRIC_TYPE_FLOAT,  "P_L1_redelivery",    "whiskeygrid/energy/mains/power/L1_redelivery",       "L1 redelivery power" },
  { "1-0:32.7.0",   METRIC_TYPE_FLOAT,  "V_L1",               "whiskeygrid/energy/mains/voltage/L1",                "L1 voltage" },
  { "1-0:31.7.0",   METRIC_TYPE_FLOAT,  "I_L1",               "whiskeygrid/energy/mains/current/L1",                "L1 current" },
  // { "1-0:41.7.0",   METRIC_TYPE_FLOAT,  "P_L2",               "whiskeygrid/energy/mains/power/L2",                  "L2 power" },
  // { "1-0:42.7.0",   METRIC_TYPE_FLOAT,  "P_L2_redelivery",    "whiskeygrid/energy/mains/power/L2_redelivery",       "L2 redelivery power" },
  // { "1-0:52.7.0",   METRIC_TYPE_FLOAT,  "V_L2",               "whiskeygrid/energy/mains/voltage/L2",                "L2 voltage" },
  // { "1-0:51.7.0",   METRIC_TYPE_FLOAT,  "I_L2",               "whiskeygrid/energy/mains/current/L2",                "L2 current" },
  // { "1-0:61.7.0",   METRIC_TYPE_FLOAT,  "P_L3",               "whiskeygrid/energy/mains/power/L3",                  "L3 power" },
  // { "1-0:62.7.0",   METRIC_TYPE_FLOAT,  "P_L3_redelivery",    "whiskeygrid/energy/mains/power/L3_redelivery",       "L3 redelivery power" },
  // { "1-0:72.7.0",   METRIC_TYPE_FLOAT,  "V_L3",               "whiskeygrid/energy/mains/voltage/L3",                "L3 voltage" },
  // { "1-0:71.7.0",   METRIC_TYPE_FLOAT,  "I_L3",               "whiskeygrid/energy/mains/current/L3",                "L3 current" },

  { "0-1:24.1.0",   METRIC_TYPE_BARE,       "",               "",   "gas meter device type" },
  { "0-1:96.1.0",   METRIC_TYPE_META_TEXT,  "gas_meter_sn",   "",   "gas meter serial number" },

  { "0-1:24.2.1",   METRIC_TYPE_GAS,    "gas_reading",    "whiskeygrid/energy/gas/reading",   "gas meter last reading" },

  { "0-0:96.7.21",  METRIC_TYPE_BARE,   "failures",       "whiskeygrid/energy/mains/report/powerfailures",        "power failures" },
  { "0-0:96.7.9",   METRIC_TYPE_BARE,   "long_failures",  "whiskeygrid/energy/mains/report/powerfailures_long",   "long power failures" },
  { "1-0:99.97.0",  METRIC_TYPE_OTHER,  "failure_log",    "whiskeygrid/energy/mains/report/powerfailure_details", "power failure event log" },
  { "1-0:32.32.0",  METRIC_TYPE_BARE,   "L1_sags",        "whiskeygrid/energy/mains/report/voltage_sags/L1",      "L1 voltage sags" },
  // { "1-0:52.32.0",  METRIC_TYPE_BARE,   "L2_sags",        "whiskeygrid/energy/mains/report/voltage_sags/L2",      "L2 voltage sags" },
  // { "1-0:72.32.0",  METRIC_TYPE_BARE,   "L3_sags",        "whiskeygrid/energy/mains/report/voltage_sags/L3",      "L3 voltage sags" },
  { "1-0:32.36.0",  METRIC_TYPE_BARE,   "L1_swells",      "whiskeygrid/energy/mains/report/voltage_swells/L1",    "L1 voltage swells" },
  // { "1-0:52.36.0",  METRIC_TYPE_BARE,   "L2_swells",      "whiskeygrid/energy/mains/report/voltage_swells/L2",    "L2 voltage swells" },
  // { "1-0:72.36.0",  METRIC_TYPE_BARE,   "L3_swells",      "whiskeygrid/energy/mains/report/voltage_swells/L3",    "L3 voltage swells" },

  { "0-0:96.13.0",  METRIC_TYPE_TEXT,   "message",   "whiskeygrid/energy/mains/message",   "text message" }  // text message
};

const int interval = 15000;
int lastTelegram = -15000;

const int RTSpin = 5;
// #define RTS_INVERT_LOGIC  // to invert the RTS output logic, e.g. to use with a NPN output inverter when the GPIO output is too low, uncomment this line:

byte bufferIn[768];
int readLength;
char receivedCRC[5];

// byte bufferIn[768] = "/ISK5\\2M550E-1012\r\n\r\n1-3:0.2.8(50)\r\n0-0:1.0.0(190827155511S)\r\n0-0:96.1.1(4D455445525F53455249414C235F484558)\r\n1-0:1.8.1(000057.460*kWh)\r\n1-0:1.8.2(000037.300*kWh)\r\n1-0:2.8.1(000000.000*kWh)\r\n1-0:2.8.2(000000.000*kWh)\r\n0-0:96.14.0(0002)\r\n1-0:1.7.0(00.498*kW)\r\n1-0:2.7.0(00.000*kW)\r\n0-0:96.7.21(00008)\r\n0-0:96.7.9(00002)\r\n1-0:99.97.0()\r\n1-0:32.32.0(00005)\r\n1-0:32.36.0(00001)\r\n0-0:96.13.0()\r\n1-0:32.7.0(235.4*V)\r\n1-0:31.7.0(002*A)\r\n1-0:21.7.0(00.454*kW)\r\n1-0:22.7.0(00.000*kW)\r\n0-1:24.1.0(003)\r\n0-1:96.1.0(4D455445525F53455249414C235F484558)\r\n0-1:24.2.1(190827155507S)(00004.380*m3)\r\n!";
// int readLength = strlen((char*)bufferIn);
// char receivedCRC[5] = "ECDF";

WiFiClient espClient;
PubSubClient client(espClient);
char msg[50];


#ifdef RTS_INVERT_LOGIC
  #define RTS_HIGH LOW
  #define RTS_LOW HIGH
#else
  #define RTS_HIGH HIGH
  #define RTS_LOW LOW
#endif

void setup()
{
  Serial.begin(115200);
  Serial.setTimeout(100);
  pinMode(RTSpin, OUTPUT);
  digitalWrite(RTSpin, RTS_LOW);

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
  digitalWrite(RTSpin, RTS_HIGH);
  delay(100);
  digitalWrite(RTSpin, RTS_LOW);
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
  int i = 0;
  char *lineptr;
  String line, ident, value, tmpValue, timestamp, gasTimestamp;
  bool timestampDST, gasTimestampDST;
  byte hexbuf[3];
  metricDef* metric;
  bool allowPublish;

  #ifdef INFLUX
    String influxLine;
    influxLine.reserve(1024);

    String influxTags;
    influxTags.reserve(384);
    influxTags.concat(influxMeasurement);
    influxTags.concat(',');

    String influxFields;
    influxFields.reserve(640);
  #endif

  Serial.print("==== START OF TELEGRAM ====\n\n");

  lineptr = strtok(telegram, "\r\n");
  line = String(lineptr);   // need to do anything with the device model header?

  Serial.print(line + "\n\n");

  while (lineptr = strtok(NULL, "\r\n")) {
    line = String(lineptr);

    if (line.charAt(0) == '!') {
      Serial.println("==== END OF TELEGRAM ====");

      #ifdef INFLUX
        // remove commas from ends of tags and fields strings
        influxTags.remove(influxTags.length() - 1);
        influxFields.remove(influxFields.length() - 1);

        // post influx string to influxTopic
        influxLine.concat(influxTags + " " + influxFields);
        Serial.println(influxLine);
        client.publish(influxTopic, influxLine.c_str(), true);
      #endif

      break;
    }

    Serial.print(++i);
    Serial.print(": ");
    Serial.print(line);

    if (line.length() >= 8) {
      allowPublish = true;

      if (line.lastIndexOf('(') != -1) {
        value = line.substring(line.lastIndexOf('(') + 1, line.lastIndexOf(')'));

        ident = line.substring(0, line.indexOf('('));

        metric = getMetricDef(ident.c_str());

        if (metric != NULL) {
          switch (metric->type) {
            case METRIC_TYPE_BARE:
              #ifdef INFLUX
                if (strlen(metric->influx_column) > 0) {
                  influxFields.concat(metric->influx_column);
                  influxFields.concat('=');
                  influxFields.concat(value);
                  influxFields.concat(',');
                }
              #endif

              // if (strcmp("0-0:1.0.0", metric->ident) == 0) { // timestamp
              //   timestamp = value.substring(0, value.length() - 1); // cut off DST indicator
              //   timestampDST = value.charAt(value.length() - 1) == 'S';
              // }

              break;

            case METRIC_TYPE_GAS:
              gasTimestamp = line.substring(line.indexOf('(') + 1, line.indexOf(')') - 1);
              gasTimestampDST = line.charAt(line.indexOf(')') - 1) == 'S';
              Serial.print("\t@{" + gasTimestamp + "}");

              allowPublish = gasTimestamp > lastGasTimestamp;

              lastGasTimestamp = gasTimestamp;

            case METRIC_TYPE_FLOAT:
              value.replace("*", " ");

              #ifdef INFLUX
                if (allowPublish && strlen(metric->influx_column) > 0) {
                  influxFields.concat(metric->influx_column);
                  influxFields.concat('=');
                  influxFields.concat(value.substring(0, value.lastIndexOf(' ')));
                  influxFields.concat(',');
                }
              #endif

              break;

            case METRIC_TYPE_TEXT:
            case METRIC_TYPE_META_TEXT:
              tmpValue = "";
              tmpValue.reserve(value.length()/2);

              for (int i = 0; i < value.length()/2; i++) {
                value.substring(i*2).getBytes(hexbuf, 3);
                hexbuf[2] = 0;

                tmpValue.concat((char)strtol((char*)hexbuf, NULL, 16));
              }
              value = tmpValue;

              if (metric->type == METRIC_TYPE_TEXT) {
                #ifdef INFLUX
                  if (strlen(metric->influx_column) > 0) {
                    influxFields.concat(metric->influx_column);
                    influxFields.concat("=\"");
                    influxFields.concat(value);
                    influxFields.concat("\",");
                  }
                #endif

                break;
              }

            case METRIC_TYPE_META:

              if (strcmp("1-3:0.2.8", metric->ident) == 0) { // SMR protocol version
                //TODO: use this value to adjust protocol handling

              } else if (strcmp("0-0:96.1.1", metric->ident) == 0) {  // device serial number
                // is this useful?

              } else if (strcmp("0-1:96.1.0", metric->ident) == 0) {  // gas meter serial number
                // is this useful?
              }

              #ifdef INFLUX
                if (strlen(metric->influx_column) > 0) {
                  influxTags.concat(metric->influx_column);
                  influxTags.concat("=\"");
                  influxTags.concat(value);
                  influxTags.concat("\",");
                }
              #endif

              break;
          }
        }

        if (strlen(metric->description) > 0) {
          Serial.print("  -> ");
          Serial.print(metric->description);
          Serial.println(" [" + value + "]");
        }

        if (allowPublish && strlen(metric->topic) > 0) {
          Serial.print(metric->topic);
          Serial.print(' ');
          Serial.println(value.c_str());
          client.publish(metric->topic, value.c_str(), true);
        }
      }
    }
    Serial.print('\n');
  }

}


metricDef* getMetricDef(const char* ident)
{
  for (int i = 0; i < sizeof(metricDefs)/sizeof(metricDefs[0]); i++)
  {
    if (strcmp(ident, metricDefs[i].ident) == 0) {
      return &metricDefs[i];
    }
  }
  return NULL;
}


// WiFi set-up and reconnect functions

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
      client.publish(connTopic, msg);
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
