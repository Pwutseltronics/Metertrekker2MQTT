#include <WiFiSettings.h>
#include <LittleFS.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <SoftwareSerial.h>

#include "Crc16.h"
Crc16 CRC;

#include "settings.h"

byte bufferIn[768];
int readLength;
char receivedCRC[5];

WiFiClient espClient;
PubSubClient client(espClient);
char msg[50];

String mqtt_host;
int mqtt_port;
String mqtt_notify_topic;

#ifdef INFLUX
    String influx_topic;
    String influx_electricity_measurement;
    String influx_gas_measurement;
#endif

// Tx pin must be specified but is overwritten if Rx pin is the same, thus disabling Tx
SoftwareSerial P1(RX_PIN, RX_PIN, true);

#ifdef RTS_INVERT_LOGIC
    #define RTS_HIGH LOW
    #define RTS_LOW HIGH
#else
    #define RTS_HIGH HIGH
    #define RTS_LOW LOW
#endif

unsigned long lastTelegram = -15000;  // time of last telegram reception
unsigned int interval;

void setup()
{
    Serial.begin(115200);
    Serial.setTimeout(100);

    LittleFS.begin();  // Will format on the first run after failing to mount
    setup_wifi();

    pinMode(RX_PIN, INPUT);
    P1.begin(115200);

    pinMode(RTS_PIN, OUTPUT);
    digitalWrite(RTS_PIN, RTS_LOW);

    client.setServer(mqtt_host.c_str(), mqtt_port);
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
        if (P1.available() > 0) {     // check for incoming serial data
            if (P1.peek() == '/') {     // check for telegram header
                readLength = P1.readBytesUntil('!', bufferIn, 766);
                bufferIn[readLength++] = '!';
                bufferIn[readLength] = 0;

                Serial.println("Telegram received!");

                Serial.printf("telegram length: %zu\n", readLength);

                P1.readBytes(receivedCRC, 4);
                receivedCRC[4] = 0;
                Serial.printf("read CRC: %s\n", receivedCRC);

                if (verifyTelegram(bufferIn, receivedCRC)) {
                    Serial.print("Telegram valid!\n\n");

                    lastTelegram = millis();
                    parseTelegram((char*)bufferIn);
                } else {
                    Serial.println("Telegram NOT valid!");
                    delay(200);
                }

                Serial.println();

            } else P1.read();

        } else {
            requestTelegram();

            Serial.print("Waiting for telegram");
            for (int i = 0; i < 20; i++) {
                if (P1.available() > 0)  break;

                Serial.print('.');
                delay(15);
            }
            Serial.print("\n\n");
        }
    } else if (P1.available() > 0)  P1.read(); // discard serial input
}


void requestTelegram()
{
    Serial.println("Requesting telegram...");
    digitalWrite(RTS_PIN, RTS_HIGH);
    delay(100);
    digitalWrite(RTS_PIN, RTS_LOW);
}


bool verifyTelegram(const byte* telegram, const char* checkCRC)
{
    char calculatedCRC[5] = "";

    sprintf(calculatedCRC, "%X", CRC.fastCrc((uint8_t*)telegram, 0, readLength, true, true, 0x8005, 0x0000, 0x0000, 0x8000, 0xffff));

    Serial.printf("calculated CRC: %s\n", calculatedCRC);

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
    bool allowPublish, publishGas = 0;

    #ifdef INFLUX
        String influxLine;
        influxLine.reserve(1024);

        String influxTags;
        influxTags.reserve(384);
        influxTags.concat(influx_electricity_measurement);
        influxTags.concat(',');

        String influxFields;
        influxFields.reserve(640);

        // separate variables for handling gas measurement
        String influxGasLine, influxGasTags, influxGasFields;
        influxGasLine.reserve(128);
        influxGasTags.reserve(64);
        influxGasFields.reserve(64);
        influxGasTags.concat(influx_gas_measurement);
        influxGasTags.concat(',');
    #endif

    Serial.print("==== START OF TELEGRAM ====\n\n");

    lineptr = strtok(telegram, "\r\n");
    line = String(lineptr);   // TODO: need to do anything with the device model header?

    Serial.printf("%s\n\n", line.c_str());

    while (lineptr = strtok(NULL, "\r\n")) {
        line = String(lineptr);

        if (line.charAt(0) == '!') {
            Serial.println("==== END OF TELEGRAM ====");

            #ifdef INFLUX
                // remove commas from ends of tags and fields strings
                influxTags.remove(influxTags.length() - 1);
                influxFields.remove(influxFields.length() - 1);

                influxGasTags.remove(influxGasTags.length() - 1);
                influxGasFields.remove(influxGasFields.length() - 1);

                // post influx electricity measurement to influxTopic
                influxLine.concat(influxTags + " " + influxFields);
                Serial.println(influxLine);
                client.publish(influx_topic.c_str(), influxLine.c_str(), true);

                if (publishGas) {
                    // post influx gas measurement to influxTopic
                    influxGasLine.concat(influxGasTags + " " + influxGasFields);
                    Serial.println(influxGasLine);
                    client.publish(influx_topic.c_str(), influxGasLine.c_str(), true);
                }
            #endif

            break;
        }

        Serial.printf("%d: %s\n", ++i, line.c_str());

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
                                    appendInfluxValue(&influxFields, metric->influx_column, value, false);
                                }
                            #endif

                            break;

                        case METRIC_TYPE_GAS:
                            gasTimestamp = line.substring(line.indexOf('(') + 1, line.indexOf(')'));
                            Serial.printf("\t@{%s}\n", gasTimestamp.c_str());

                            publishGas = allowPublish = (gasTimestamp > lastGasTimestamp);

                            lastGasTimestamp = gasTimestamp;

                            value.replace("*", " ");

                            #ifdef INFLUX
                                if (publishGas && strlen(metric->influx_column) > 0) {
                                    // add gas measurement timestamp to gas measurement line
                                    appendInfluxValue(&influxGasFields, "timestamp", gasTimestamp, true);

                                    // add gas reading to gas measurement line
                                    appendInfluxValue(&influxGasFields, metric->influx_column, value.substring(0, value.lastIndexOf(' ')), false);
                                }
                            #endif

                            break;

                        case METRIC_TYPE_FLOAT:
                            value.replace("*", " ");

                            #ifdef INFLUX
                                if (strlen(metric->influx_column) > 0) {
                                    appendInfluxValue(&influxFields, metric->influx_column, value.substring(0, value.lastIndexOf(' ')), false);
                                }
                            #endif

                            break;

                        case METRIC_TYPE_TEXT:
                        case METRIC_TYPE_META_TEXT:
                            tmpValue = "";
                            tmpValue.reserve(value.length()/2);

                            for (size_t i = 0; i < value.length()/2; i++) {
                                value.substring(i*2).getBytes(hexbuf, 3);
                                hexbuf[2] = 0;

                                tmpValue.concat((char)strtol((char*)hexbuf, NULL, 16));
                            }
                            value = tmpValue;

                            if (metric->type == METRIC_TYPE_TEXT) {
                                #ifdef INFLUX
                                    if (strlen(metric->influx_column) > 0) {
                                        appendInfluxValue(&influxFields, metric->influx_column, value, true);
                                    }
                                #endif

                                break;
                            }

                        case METRIC_TYPE_META:

                            if (strcmp("0-0:1.0.0", metric->ident) == 0) { // timestamp
                                #ifdef INFLUX
                                    if (strlen(metric->influx_column) > 0)
                                        appendInfluxValue(&influxFields, metric->influx_column, value, true);
                                #endif

                            } else if (strcmp("0-1:96.1.0", metric->ident) == 0) {  // gas meter serial number
                                #ifdef INFLUX
                                    if (strlen(metric->influx_column) > 0)
                                        appendInfluxValue(&influxGasTags, metric->influx_column, value, true);
                                #endif

                            // } else if (strcmp("1-3:0.2.8", metric->ident) == 0) { // SMR protocol version
                            //   //TODO: use this value to adjust protocol handling

                            } else {
                                #ifdef INFLUX
                                    if (strlen(metric->influx_column) > 0) {
                                        appendInfluxValue(&influxTags, metric->influx_column, value, true);
                                    }
                                #endif
                            }

                            break;
                    }
                }

                if (strlen(metric->description) > 0) {
                    Serial.printf("  -> %s [%s]\n", metric->description, value.c_str());
                }

                if (allowPublish && strlen(metric->topic) > 0) {
                    Serial.printf("%s %s\n", metric->topic, value.c_str());
                    client.publish(metric->topic, value.c_str(), true);
                }
            }
        }
        Serial.print('\n');
    }

}


metricDef* getMetricDef(const char* ident)
{
    for (size_t i = 0; i < sizeof(metricDefs)/sizeof(metricDefs[0]); i++)
    {
        if (strcmp(ident, metricDefs[i].ident) == 0) {
            return &metricDefs[i];
        }
    }
    return NULL;
}


void appendInfluxValue(String* influxString, char* column_name, String value, bool valueIsString)
{
    influxString->concat(column_name);
    if (valueIsString)
        influxString->concat("=\"" + value + "\",");
    else
        influxString->concat('=' + value + ',');
}


// WiFi and MQTT setup functions

void setup_wifi() {
    WiFiSettings.hostname = printf("%s-%06" PRIx32, d_client_id, ESP.getChipId());

    mqtt_host = WiFiSettings.string("mqtt-host", d_mqtt_host, "MQTT server host");
    mqtt_port = WiFiSettings.integer("mqtt-port", d_mqtt_port, "MQTT server port");
    mqtt_notify_topic = WiFiSettings.string("mqtt-notify-topic", d_notify_topic, "MQTT connect notification topic");

    #ifdef INFLUX
        influx_topic =
            WiFiSettings.string("influx-topic", d_influx_topic, "Influx MQTT topic");
        influx_electricity_measurement =
            WiFiSettings.string("influx-electricity-measurement", d_influx_electricity_measurement, "Influx electricity measurement");
        influx_gas_measurement =
            WiFiSettings.string("influx-gas-measurement", d_influx_gas_measurement, "Influx gas measurement");
    #endif

    interval = WiFiSettings.integer("fetch-interval", 10, 3600, d_interval, "Measuring interval")*1000;

    WiFiSettings.connect();

    Serial.print("Password: ");
    Serial.println(WiFiSettings.password);
}

void reconnect()
{
    int connLoseMillis = millis();
    // Loop until connected
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");

        if (client.connect(WiFiSettings.hostname.c_str())) { // Attempt to connect
            Serial.println("connected");

            // Post connect message to MQTT topic once connected
            int connectMillis = millis();
            float lostSeconds = (connectMillis - connLoseMillis) / 1000;
            sprintf(msg, "%s (re)connected after %.1f", WiFiSettings.hostname.c_str(), lostSeconds);
            client.publish(mqtt_notify_topic.c_str(), msg);
            Serial.println(msg);

        } else {
            Serial.printf("failed, rc=%d; try again in 5 seconds", client.state());
            delay(5000);  // Wait 5 seconds before retrying
        }
    }
}
