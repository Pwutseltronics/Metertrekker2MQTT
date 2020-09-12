#include <WiFiSettings.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <MQTT.h>
#include <SoftwareSerial.h>

#include "Crc16.h"
Crc16 CRC;

#include "settings.h"

WiFiClient network;
MQTTClient mqttClient(1024);    // specify buffer size


/* The following settings can be set in the WiFi portal but default to values in settings.h */

String mqtt_host;
String mqtt_topic_root;
String mqtt_notify_topic;

#ifdef INFLUX
    String influx_topic, influx_electricity_measurement, influx_gas_measurement;
#endif


SoftwareSerial P1;


/* Helper functions/macros */

#define Sprintf(f, ...) ({ char* s; asprintf(&s, f, __VA_ARGS__); String r = s; free(s); r; })

// Send RTS signal (and set LED correspondingly)
void set_RTS(bool s)
{
    digitalWrite(RTS_PIN, s ? RTS_HIGH : RTS_LOW);
    digitalWrite(LED_BUILTIN, !s);
}

// Return string content from stored file
String slurp(const String& fn) {
    File f = LittleFS.open(fn, "r");
    String r = f.readString();
    f.close();
    return r;
}

// Store string to file
void spurt(const String& fn, const String& content) {
    File f = LittleFS.open(fn, "w");
    f.print(content);
    f.close();
}

/* These forward declarations are not added by the Arduino preprocessor because they have default
 * arguments (retain and qos). See https://github.com/arduino/arduino-preprocessor/issues/12 */
bool mqtt_publish(const String &topic_path, const String &message, bool retain = true, int qos = 0);


unsigned int interval;
unsigned int timeout;

int dsmr_version = -1;

void setup()
{
    Serial.begin(115200);

    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(RTS_PIN, OUTPUT);
    set_RTS(LOW);

    LittleFS.begin();  // Will format on the first run after failing to mount
    setup_wifi();
    setup_ota();

    mqttClient.begin(mqtt_host.c_str(), network);
    connect_mqtt();

    P1.begin(115200, SWSERIAL_8N1, RX_PIN, -1, true, 768);  // Tx disabled
    P1.setTimeout(50);
}


void loop()
{
    static bool first_telegram = true;
    static unsigned long last_telegram_time = 0;

    ArduinoOTA.handle();
    mqttClient.loop();

    if (!mqttClient.connected()) {
        connect_mqtt();
    }

    if (millis() - last_telegram_time > interval || first_telegram) {
        if (!P1.available())  requestTelegram();

        while (P1.available()) {
            if (P1.peek() == '/') {     // check for telegram header
                char buffer_in[768];
                size_t read_length = P1.readBytesUntil('!', buffer_in, 766);
                buffer_in[read_length++] = '!';
                buffer_in[read_length] = 0;

                set_RTS(LOW);

                Serial.printf("Telegram length: %zu\n", read_length);

                bool telegram_valid;
                if (dsmr_version >= 42) {   // before DSMR 4.2, telegrams did not contain a CRC tag at the end
                    char received_crc[5];
                    P1.readBytes(received_crc, 4);
                    received_crc[4] = 0;

                    Serial.printf("read CRC: %s\r\n", received_crc);

                    telegram_valid = crcVerifyTelegram((byte*)buffer_in, read_length, received_crc);
                } else {
                    telegram_valid = bracketVerifyTelegram(buffer_in, read_length);
                }

                if (telegram_valid) {
                    if (dsmr_version >= 42) Serial.print("Telegram valid!\r\n\n");  // can't be so sure from just counting the brackets (< DSMR 4.2)

                    first_telegram = false;
                    last_telegram_time = millis();
                    parseTelegram(buffer_in);

                    Serial.printf("Free heap: %zu bytes\r\n", ESP.getFreeHeap());
                } else {
                    Serial.println("Telegram NOT valid!");
                    Serial.println("\nTELEGRAM >>>>>>");
                    Serial.println(buffer_in);
                    Serial.print("<<<<<< TELEGRAM\r\n\n");
                    delay(400);
                }

                Serial.println();

            } else P1.read();
        }

        if (millis() - last_telegram_time > interval + timeout) {
            timeoutHandler();
        }
    } else if (P1.available() > 0)  P1.read(); // discard serial input
}

// Request a telegram from the meter; returns when serial data comes in
void requestTelegram()
{
    Serial.println("Requesting telegram...");
    set_RTS(HIGH);

    unsigned long i = 0;
    Serial.print("Waiting for telegram");

    while (!P1.available()) {
        if (i++ > timeout*10)  timeoutHandler();

        delayMicroseconds(100);
        Serial.print('.');
    }
    Serial.print("\r\n\n");
}


void timeoutHandler()
{
    Serial.println(F("\r\n\nTimeout, starting portal"));
    WiFiSettings.portal();
}

// Verify a telegram using a given CRC16 code
bool crcVerifyTelegram(const byte* telegram, size_t length, const char* check_crc)
{
    char calculated_crc[5] = "";

    sprintf(calculated_crc, "%4X", CRC.fastCrc((uint8_t*)telegram, 0, length, true, true, 0x8005, 0x0000, 0x0000, 0x8000, 0xffff));

    Serial.printf("calculated CRC: %s\r\n", calculated_crc);

    return strncmp(calculated_crc, check_crc, 4) == 0;
}

bool bracketVerifyTelegram(const char* telegram, size_t length)
{
    int counter = 0;
    for (size_t i = 0; i < length; i++)
    {
        switch (telegram[i]) {
            case '(':
                counter++;
                break;
            case ')':
                counter--;
                break;
        }
    }

    return counter == 0;
}

// Parse telegram, process contained metrics
void parseTelegram(char* telegram)
{
    int ln = 0;
    char *lineptr;
    String line, ident, value, tmp_value, timestamp, gas_timestamp;
    bool timestampDST, gasTimestampDST;
    byte hexbuf[3];
    metricDef* metric;
    bool allow_publish, publish_gas = 0;

    #ifdef INFLUX
        String influx_tags;
        influx_tags.reserve(384);

        String influx_fields;
        influx_fields.reserve(640);

        // separate variables for handling gas measurement
        String influx_gas_tags, influx_gas_fields;
        influx_gas_tags.reserve(64);
        influx_gas_fields.reserve(64);
    #endif

    Serial.print(F("==== START OF TELEGRAM ====\r\n\n"));

    lineptr = strtok(telegram, "\r\n");
    line = String(lineptr);   // TODO: need to do anything with the device model header?

    Serial.printf("%s\r\n\n", line.c_str());

    while ((lineptr = strtok(NULL, "\r\n"))) {
        line = String(lineptr);

        if (line.charAt(0) == '!') {
            Serial.println(F("==== END OF TELEGRAM ===="));

            #ifdef INFLUX
                // remove commas from ends of tags and fields strings
                influx_tags.remove(influx_tags.length() - 1);
                influx_fields.remove(influx_fields.length() - 1);

                influx_gas_tags.remove(influx_gas_tags.length() - 1);
                influx_gas_fields.remove(influx_gas_fields.length() - 1);

                influx_publish(influx_electricity_measurement, influx_fields, influx_tags);

                if (publish_gas) {
                    influx_publish(influx_gas_measurement, influx_gas_fields, influx_gas_tags);
                }
            #endif

            break;
        }

        Serial.printf("%d: %s", ++ln, line.c_str());

        if (line.length() >= 8) {
            allow_publish = true;

            if (line.lastIndexOf('(') != -1) {
                value = line.substring(line.lastIndexOf('(') + 1, line.lastIndexOf(')'));
                ident = line.substring(0, line.indexOf('('));
                metric = get_metric_def(ident.c_str());

                if (metric != NULL) {
                    switch (metric->type) {
                        case METRIC_TYPE_BARE:
                            #ifdef INFLUX
                                if (strlen(metric->influx_column) > 0) {
                                    append_influx_value(influx_fields, metric->influx_column, value, false);
                                }
                            #endif

                            break;

                        case METRIC_TYPE_GAS:
                            gas_timestamp = line.substring(line.indexOf('(') + 1, line.indexOf(')'));
                            Serial.printf("\t@{%s}\r\n", gas_timestamp.c_str());

                            publish_gas = allow_publish = (gas_timestamp > slurp("/last-gas-timestamp"));

                            spurt("/last-gas-timestamp", gas_timestamp);

                            value.replace("*", " ");

                            #ifdef INFLUX
                                if (publish_gas && strlen(metric->influx_column) > 0) {
                                    // add gas measurement timestamp to gas measurement line
                                    append_influx_value(influx_gas_fields, "timestamp", gas_timestamp, true);

                                    // add gas reading to gas measurement line
                                    append_influx_value(influx_gas_fields, metric->influx_column, value.substring(0, value.lastIndexOf(' ')), false);
                                }
                            #endif

                            break;

                        case METRIC_TYPE_FLOAT:
                            value.replace("*", " ");

                            #ifdef INFLUX
                                if (strlen(metric->influx_column) > 0) {
                                    append_influx_value(influx_fields, metric->influx_column, value.substring(0, value.lastIndexOf(' ')), false);
                                }
                            #endif

                            break;

                        case METRIC_TYPE_TEXT:
                        case METRIC_TYPE_META_TEXT:
                            tmp_value.reserve(value.length()/2);

                            for (unsigned int i = 0; i < value.length()/2; i++) {
                                value.substring(i*2).getBytes(hexbuf, 3);
                                hexbuf[2] = 0;

                                tmp_value.concat((char)strtol((char*)hexbuf, NULL, 16));
                            }
                            value = tmp_value;
                            tmp_value.clear();

                            if (metric->type == METRIC_TYPE_TEXT) {
                                allow_publish = value == slurp("/last-" + String(metric->influx_column));
                                if (allow_publish) spurt("/last-" + String(metric->influx_column), value);

                                #ifdef INFLUX
                                    if (allow_publish && strlen(metric->influx_column) > 0) {
                                        append_influx_value(influx_fields, metric->influx_column, value, true);
                                    }
                                #endif

                                break;
                            }

                        case METRIC_TYPE_META:

                            if (strcmp("1-3:0.2.8", metric->ident) == 0) { // SMR protocol version
                                dsmr_version = value.toInt();

                            } else if (strcmp("0-0:1.0.0", metric->ident) == 0) { // timestamp
                                #ifdef INFLUX
                                    if (strlen(metric->influx_column) > 0)
                                        append_influx_value(influx_fields, metric->influx_column, value, true);
                                #endif

                            } else if (strcmp("0-1:96.1.0", metric->ident) == 0) {  // gas meter serial number
                                #ifdef INFLUX
                                    if (strlen(metric->influx_column) > 0)
                                        append_influx_value(influx_gas_tags, metric->influx_column, value, true);
                                #endif

                            } else {
                                #ifdef INFLUX
                                    if (strlen(metric->influx_column) > 0) {
                                        append_influx_value(influx_tags, metric->influx_column, value, false);  // no quotes around tag values!
                                    }
                                #endif
                            }

                            break;
                    }

                    if (strlen(metric->description) > 0) {
                        Serial.printf("  -> %s [%s]\r\n", metric->description, value.c_str());
                    }

                    if (allow_publish && strlen(metric->mqtt_path) > 0) {
                        mqtt_publish(metric->mqtt_path, value);
                    }
                } else {
                    Serial.printf("NOTIFY: unknown OBIS identity: %s\r\n", ident.c_str());
                }
            }
        }
        Serial.print('\n');
    }

}

// Get metric definition for given OBIS identity as defined in settings.h
metricDef* get_metric_def(const char* ident)
{
    for (size_t i = 0; i < sizeof(metricDefs)/sizeof(metricDefs[0]); i++)
    {
        if (strcmp(ident, metricDefs[i].ident) == 0) {
            return &metricDefs[i];
        }
    }
    return NULL;
}

// Publish a message to an MQTT topic
bool mqtt_publish(const String &topic_path, const String &message, bool retain, int qos)
{
    String topic = mqtt_topic_root + topic_path;
    Serial.printf("--> %s  %s\r\n", topic.c_str(), message.c_str());
    return mqttClient.publish(topic, message, retain, qos);
}

#ifdef INFLUX
// Publish to the influx MQTT topic in influx format
void influx_publish(const String &measurement, const String &fields, const String &tags)
{
    String influx_line = Sprintf("%s%s%s ", measurement.c_str(), tags.length() ? "," : "", tags.c_str()) + fields;
    mqtt_publish(influx_topic, influx_line, false, 1);
}

// Append a metric to an influx line-format string
void append_influx_value(String &influxString, char* column_name, String value, bool valueIsString)
{
    influxString.concat(column_name);
    influxString.concat(valueIsString ? "=\"" + value + "\"," : '=' + value + ',');
}
#endif

// WiFi and MQTT setup functions

void setup_wifi() {
    WiFiSettings.hostname = Sprintf("%s-%06" PRIx32, client_id, ESP.getChipId());

    mqtt_host = WiFiSettings.string("mqtt-host", d_mqtt_host, F("MQTT server host"));
    mqtt_topic_root = WiFiSettings.string("mqtt-root", d_mqtt_topic_root, F("MQTT topic root"));
    mqtt_notify_topic = WiFiSettings.string("mqtt-notify-topic", d_notify_topic, F("MQTT connect notification topic"));

    #ifdef INFLUX
        influx_topic =
            WiFiSettings.string("influx-topic", d_influx_topic, F("Influx MQTT topic"));
        influx_electricity_measurement =
            WiFiSettings.string("influx-electricity-measurement", d_influx_electricity_measurement, F("Influx electricity measurement"));
        influx_gas_measurement =
            WiFiSettings.string("influx-gas-measurement", d_influx_gas_measurement, F("Influx gas measurement"));
    #endif

    interval = WiFiSettings.integer("fetch-interval", 10, 3600, d_interval, F("Measuring interval"));
    timeout = WiFiSettings.integer("fetch-timeout", 10, 120, d_timeout, F("Timeout to portal"));

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

// Set up OTA update
void setup_ota()
{
    ArduinoOTA.setHostname(WiFiSettings.hostname.c_str());
    ArduinoOTA.setPassword(WiFiSettings.password.c_str());
    ArduinoOTA.begin();
}

void connect_mqtt()
{
    unsigned long connection_lose_time = millis();

    Serial.print(F("Attempting MQTT connection..."));
    while (!mqttClient.connect(WiFiSettings.hostname.c_str())) {   // Loop until connected
        Serial.print('.');
        delay(500);

        // start portal after 30 seconds without connectivity
        if (millis() - connection_lose_time > 30e3) WiFiSettings.portal();
    }
    Serial.println();

    String message = Sprintf("%s (re)connected after %.1fs", WiFiSettings.hostname.c_str(), (millis() - connection_lose_time) / 1000.0);

    // Post connect message to MQTT topic once connected
    mqtt_publish(mqtt_notify_topic, message, false);
    Serial.println();
}
