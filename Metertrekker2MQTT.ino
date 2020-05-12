#include <WiFiSettings.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <SoftwareSerial.h>

#include "Crc16.h"
Crc16 CRC;

#include "settings.h"

WiFiClient espClient;
PubSubClient mqtt_client(espClient);

String mqtt_host;
int mqtt_port;
String mqtt_notify_topic;

#ifdef INFLUX
    String influx_topic, influx_electricity_measurement, influx_gas_measurement;
#endif

SoftwareSerial P1;

#ifdef RTS_INVERT_LOGIC
    #define RTS_HIGH LOW
    #define RTS_LOW HIGH
#else
    #define RTS_HIGH HIGH
    #define RTS_LOW LOW
#endif

void set_RTS(bool s)
{
    digitalWrite(RTS_PIN, s ? RTS_HIGH : RTS_LOW);
    digitalWrite(LED_BUILTIN, !s);
}

// Declare slurp() and spurt(); definition in WiFiSettings.cpp
String slurp(const String& fn);                         // Read file and return content
void spurt(const String& fn, const String& content);    // Write content to file

#define Sprintf(f, ...) ({ char* s; asprintf(&s, f, __VA_ARGS__); String r = s; free(s); r; })

long lastTelegram;
unsigned int interval;
unsigned int timeout;

void setup()
{
    Serial.begin(115200);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    LittleFS.begin();  // Will format on the first run after failing to mount
    setup_wifi();
    setup_ota();

    P1.begin(115200, SWSERIAL_8N1, RX_PIN, -1, true, 768);  // Tx disabled
    P1.setTimeout(50);

    lastTelegram = -interval;

    pinMode(RTS_PIN, OUTPUT);
    digitalWrite(RTS_PIN, RTS_LOW);

    mqtt_client.setServer(mqtt_host.c_str(), mqtt_port);
    // mqtt_client.setCallback(callback);
}


char bufferIn[768];
size_t readLength;
char receivedCRC[5];

void loop()
{
    ArduinoOTA.handle();

    if (!mqtt_client.connected()) {
        connect_mqtt();
    }
    mqtt_client.loop();

    if (millis() - lastTelegram > interval) {
        if (!P1.available())  requestTelegram();

        while (P1.available()) {
            if (P1.peek() == '/') {     // check for telegram header
                readLength = P1.readBytesUntil('!', bufferIn, 766);
                bufferIn[readLength++] = '!';
                bufferIn[readLength] = 0;
                set_RTS(LOW);

                Serial.printf("Telegram length: %zu\n", readLength);

                P1.readBytes(receivedCRC, 4);
                receivedCRC[4] = 0;
                Serial.printf("read CRC: %s\r\n", receivedCRC);

                if (verifyTelegram((byte*)bufferIn, receivedCRC)) {
                    Serial.print("Telegram valid!\r\n\n");

                    lastTelegram = millis();
                    parseTelegram(bufferIn);
                    Serial.printf("Free heap: %zu bytes\r\n", ESP.getFreeHeap());
                } else {
                    Serial.println("Telegram NOT valid!");
                    Serial.println("\nTELEGRAM >>>>>>");
                    Serial.println(bufferIn);
                    Serial.print("<<<<<< TELEGRAM\r\n\n");
                    delay(400);
                }

                Serial.println();

            } else P1.read();
        }

        if (millis() - lastTelegram > interval + timeout) {
            Serial.println("Timeout, starting portal");
            WiFiSettings.portal();
        }
    } else if (P1.available() > 0)  P1.read(); // discard serial input
}


void requestTelegram()
{
    Serial.println("Requesting telegram...");
    set_RTS(HIGH);

    Serial.print("Waiting for telegram");
    while (!P1.available()) {
        delayMicroseconds(100);
        Serial.print('.');
    }
    Serial.print("\r\n\n");
}


bool verifyTelegram(const byte* telegram, const char* checkCRC)
{
    char calculatedCRC[5] = "";

    sprintf(calculatedCRC, "%4X", CRC.fastCrc((uint8_t*)telegram, 0, readLength, true, true, 0x8005, 0x0000, 0x0000, 0x8000, 0xffff));

    Serial.printf("calculated CRC: %s\r\n", calculatedCRC);

    return strncmp(calculatedCRC, checkCRC, 4) == 0;
}


void parseTelegram(char* telegram)
{
    int ln = 0;
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

    Serial.printf("%s\r\n\n", line.c_str());

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
                mqtt_client.publish(influx_topic.c_str(), influxLine.c_str(), true);

                if (publishGas) {
                    // post influx gas measurement to influxTopic
                    influxGasLine.concat(influxGasTags + " " + influxGasFields);
                    Serial.println(influxGasLine);
                    mqtt_client.publish(influx_topic.c_str(), influxGasLine.c_str(), true);
                }
            #endif

            break;
        }

        Serial.printf("%d: %s", ++ln, line.c_str());

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
                            Serial.printf("\t@{%s}\r\n", gasTimestamp.c_str());

                            publishGas = allowPublish = (gasTimestamp > slurp("/last-gas-timestamp"));

                            spurt("/last-gas-timestamp", gasTimestamp);

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
                            tmpValue.reserve(value.length()/2);

                            for (unsigned int i = 0; i < value.length()/2; i++) {
                                value.substring(i*2).getBytes(hexbuf, 3);
                                hexbuf[2] = 0;

                                tmpValue.concat((char)strtol((char*)hexbuf, NULL, 16));
                            }
                            value = tmpValue;
                            tmpValue.clear();

                            if (metric->type == METRIC_TYPE_TEXT) {
                                allowPublish = value == slurp("/last-" + String(metric->influx_column));
                                if (allowPublish) spurt("/last-" + String(metric->influx_column), value);

                                #ifdef INFLUX
                                    if (allowPublish && strlen(metric->influx_column) > 0) {
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

                    if (strlen(metric->description) > 0) {
                        Serial.printf("  -> %s [%s]\r\n", metric->description, value.c_str());
                    }

                    if (allowPublish && strlen(metric->topic) > 0) {
                        Serial.printf("%s %s\r\n", metric->topic, value.c_str());
                        mqtt_client.publish(metric->topic, value.c_str(), true);
                    }
                } else {
                    Serial.printf("NOTIFY: unknown OBIS identity: %s\r\n", ident.c_str());
                }
            }
        }
        Serial.print('\n');
    }

}

// Get metric definition as defined in settings.h
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

#ifdef INFLUX
// Append a metric to an influx line-format string
void appendInfluxValue(String* influxString, char* column_name, String value, bool valueIsString)
{
    influxString->concat(column_name);
    influxString->concat(valueIsString ? "=\"" + value + "\"," : '=' + value + ',');
}
#endif


// WiFi and MQTT setup functions

void setup_wifi() {
    WiFiSettings.hostname = Sprintf("%s-%06" PRIx32, d_client_id, ESP.getChipId());

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

    interval = WiFiSettings.integer("fetch-interval", 10, 3600, d_interval, "Measuring interval");
    timeout = WiFiSettings.integer("fetch-timeout", 10, 120, d_timeout, "Timeout to portal");

    interval *= 1000;   // seconds -> milliseconds
    timeout *= 1000;

    WiFiSettings.onPortal = []() {
        setup_ota();
    };
    WiFiSettings.onPortalWaitLoop = []() {
        ArduinoOTA.handle();

        if (!(millis() % 200))
            digitalWrite(LED_BUILTIN, LOW);
        else if (!(millis() % 100))
            digitalWrite(LED_BUILTIN, HIGH);
    };

    WiFiSettings.connect();

    Serial.print("Password: ");
    Serial.println(WiFiSettings.password);
}

void setup_ota()
{
    ArduinoOTA.setHostname(WiFiSettings.hostname.c_str());
    ArduinoOTA.setPassword(WiFiSettings.password.c_str());
    ArduinoOTA.begin();
}

void connect_mqtt()
{
    char msg[50];
    int connLoseMillis = millis();

    while (!mqtt_client.connected()) {   // Loop until connected
        Serial.print("Attempting MQTT connection...");

        if (mqtt_client.connect(WiFiSettings.hostname.c_str())) { // Attempt to connect
            Serial.println("connected");

            // Post connect message to MQTT topic once connected
            int connectMillis = millis();
            float lostSeconds = (connectMillis - connLoseMillis) / 1000;
            sprintf(msg, "%s (re)connected after %.1fs", WiFiSettings.hostname.c_str(), lostSeconds);
            mqtt_client.publish(mqtt_notify_topic.c_str(), msg);
            Serial.println(msg);

        } else {
            Serial.printf("failed, rc=%d; try again in 5 seconds\r\n", mqtt_client.state());
            delay(5000);  // Wait 5 seconds before retrying
        }
    }
}
