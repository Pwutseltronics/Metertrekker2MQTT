/* WiFi and MQTT stuff */
const char* ssid      = "WIFI_NETWORK";
const char* password  = "WIFI_PASSWORD";

const char* mqttServ  = "MQTT_SERV_IP";
const int   mqttPort  = 1883;
const char* clientID  = "Metertrekker";
const char* connTopic = "my_mqtt_root/debug/node_connect";

/* Comment next line to disable influx: */
#define INFLUX

#ifdef INFLUX
  const char* influxTopic = "my_mqtt_root/energy/influx";
  const char* influxElectricityMeasurement = "electricity";
  const char* influxGasMeasurement = "gas";
#endif

/* Telegram request interval */
const int interval = 15000;

/* To invert the RTS output logic, e.g. to use with an output inverter when the GPIO output is too low, uncomment this line: */
#define RTS_INVERT_LOGIC
const int RTSpin = D1;

/* Rx pin for SoftwareSerial */
const int SoftRx = RX;

/* MQTT topic & InfluxDB column configuration */

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

/* Uncomment to enable metrics for three phase power: */
// #define THREE_PHASE

/* Metric and MQTT topic settings: comment a line to disable processing/publishing of the metric.
 * Leaving the third entry (influx_column) empty will disable inclusion in the influx line, if enabled */

metricDef metricDefs[] = {
//{ "OBIS ref.",    METRIC_TYPE_CONSTANT,   "influx col.",    "MQTT topic", "description (only for serial debug output)" },
  { "1-3:0.2.8",    METRIC_TYPE_META,       "",               "",           "SMR protocol version"  },
  { "0-0:1.0.0",    METRIC_TYPE_META,       "timestamp",      "",           "telegram timestamp"    },
  { "0-0:96.1.1",   METRIC_TYPE_META_TEXT,  "meter_sn",       "",           "meter serial number"   },

  { "1-0:1.8.1",    METRIC_TYPE_FLOAT,  "delivered_low",      "my_mqtt_root/energy/mains/reading/delivered/low",    "total delivered energy (low tariff)" },
  { "1-0:1.8.2",    METRIC_TYPE_FLOAT,  "delivered_high",     "my_mqtt_root/energy/mains/reading/delivered/high",   "total delivered energy (high tariff)" },
  { "1-0:2.8.1",    METRIC_TYPE_FLOAT,  "redelivered_low",    "my_mqtt_root/energy/mains/reading/redelivered/low",  "total redelivered (low tariff)" },
  { "1-0:2.8.2",    METRIC_TYPE_FLOAT,  "redelivered_high",   "my_mqtt_root/energy/mains/reading/redelivered/high", "total redelivered (high tariff)" },
  { "0-0:96.14.0",  METRIC_TYPE_BARE,   "tariff",             "my_mqtt_root/energy/mains/reading/tariff",           "tariff" },
  { "1-0:1.7.0",    METRIC_TYPE_FLOAT,  "P_total",            "my_mqtt_root/energy/mains/power/total",              "total power" },
  { "1-0:2.7.0",    METRIC_TYPE_FLOAT,  "P_total_redelivery", "my_mqtt_root/energy/mains/power/total_redelivery",   "total redelivery power" },
  { "1-0:21.7.0",   METRIC_TYPE_FLOAT,  "P_L1",               "my_mqtt_root/energy/mains/power/L1",                 "L1 power" },
  { "1-0:22.7.0",   METRIC_TYPE_FLOAT,  "P_L1_redelivery",    "my_mqtt_root/energy/mains/power/L1_redelivery",      "L1 redelivery power" },
  { "1-0:32.7.0",   METRIC_TYPE_FLOAT,  "V_L1",               "my_mqtt_root/energy/mains/voltage/L1",               "L1 voltage" },
  { "1-0:31.7.0",   METRIC_TYPE_FLOAT,  "I_L1",               "my_mqtt_root/energy/mains/current/L1",               "L1 current" },
  #ifdef THREE_PHASE
  { "1-0:41.7.0",   METRIC_TYPE_FLOAT,  "P_L2",               "my_mqtt_root/energy/mains/power/L2",                 "L2 power" },
  { "1-0:42.7.0",   METRIC_TYPE_FLOAT,  "P_L2_redelivery",    "my_mqtt_root/energy/mains/power/L2_redelivery",      "L2 redelivery power" },
  { "1-0:52.7.0",   METRIC_TYPE_FLOAT,  "V_L2",               "my_mqtt_root/energy/mains/voltage/L2",               "L2 voltage" },
  { "1-0:51.7.0",   METRIC_TYPE_FLOAT,  "I_L2",               "my_mqtt_root/energy/mains/current/L2",               "L2 current" },
  { "1-0:61.7.0",   METRIC_TYPE_FLOAT,  "P_L3",               "my_mqtt_root/energy/mains/power/L3",                 "L3 power" },
  { "1-0:62.7.0",   METRIC_TYPE_FLOAT,  "P_L3_redelivery",    "my_mqtt_root/energy/mains/power/L3_redelivery",      "L3 redelivery power" },
  { "1-0:72.7.0",   METRIC_TYPE_FLOAT,  "V_L3",               "my_mqtt_root/energy/mains/voltage/L3",               "L3 voltage" },
  { "1-0:71.7.0",   METRIC_TYPE_FLOAT,  "I_L3",               "my_mqtt_root/energy/mains/current/L3",               "L3 current" },
  #endif

  { "0-1:24.1.0",   METRIC_TYPE_BARE,       "",               "",   "gas meter device type" },
  { "0-1:96.1.0",   METRIC_TYPE_META_TEXT,  "gas_meter_sn",   "",   "gas meter serial number" },

  { "0-1:24.2.1",   METRIC_TYPE_GAS,    "gas_reading",    "my_mqtt_root/energy/gas/reading",  "gas meter last reading" },

  { "0-0:96.7.21",  METRIC_TYPE_BARE,   "failures",       "my_mqtt_root/energy/mains/report/powerfailures",         "power failures" },
  { "0-0:96.7.9",   METRIC_TYPE_BARE,   "long_failures",  "my_mqtt_root/energy/mains/report/powerfailures_long",    "long power failures" },
  { "1-0:99.97.0",  METRIC_TYPE_OTHER,  "failure_log",    "my_mqtt_root/energy/mains/report/powerfailure_details",  "power failure event log" },
  { "1-0:32.32.0",  METRIC_TYPE_BARE,   "L1_sags",        "my_mqtt_root/energy/mains/report/voltage_sags/L1",       "L1 voltage sags" },
  { "1-0:32.36.0",  METRIC_TYPE_BARE,   "L1_swells",      "my_mqtt_root/energy/mains/report/voltage_swells/L1",     "L1 voltage swells" },
  #ifdef THREE_PHASE
  { "1-0:52.32.0",  METRIC_TYPE_BARE,   "L2_sags",        "my_mqtt_root/energy/mains/report/voltage_sags/L2",       "L2 voltage sags" },
  { "1-0:52.36.0",  METRIC_TYPE_BARE,   "L2_swells",      "my_mqtt_root/energy/mains/report/voltage_swells/L2",     "L2 voltage swells" },
  { "1-0:72.32.0",  METRIC_TYPE_BARE,   "L3_sags",        "my_mqtt_root/energy/mains/report/voltage_sags/L3",       "L3 voltage sags" },
  { "1-0:72.36.0",  METRIC_TYPE_BARE,   "L3_swells",      "my_mqtt_root/energy/mains/report/voltage_swells/L3",     "L3 voltage swells" },
  #endif

  { "0-0:96.13.0",  METRIC_TYPE_TEXT,   "message",   "my_mqtt_root/energy/mains/message",   "text message" }, // text message
  { "0-0:96.13.1",  METRIC_TYPE_TEXT,   "message",   "my_mqtt_root/energy/mains/message",   "text message" }  // text message
};
