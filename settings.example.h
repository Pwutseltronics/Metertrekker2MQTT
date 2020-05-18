/* *** Default settings, adjustable in portal *** */

/* MQTT stuff */
const char* d_mqtt_host  = "MQTT_SERV_IP";          // default MQTT host
const char* d_mqtt_topic_root = "my_mqtt_root";     // default MQTT topic root
const char* d_notify_topic = "/debug/node_connect"; // default MQTT topic for  messages regarding connection status

/* Influx stuff */
#define INFLUX   // if defined, Influx related functionality is enabled

#ifdef INFLUX
  const char* d_influx_topic = "/energy/influx";                // default MQTT topic the influx lines will be published to
  const char* d_influx_electricity_measurement = "electricity"; // default Influx measurement name for electricity data
  const char* d_influx_gas_measurement = "gas";                 // default Influx measurement name for gas data
#endif

/* Default telegram request interval and timeout */
const unsigned int d_interval = 15;         // default telegram request interval in seconds
const unsigned int d_timeout = d_interval;  // after d_timeout seconds of retrying to get a telegram, the portal will be started


/* *** Firmware configuration *** */

/* appended with the chipID and used as SSID, ArduinoOTA hostname and MQTT client ID */
const char* client_id  = "Metertrekker";

/* To invert the RTS output logic, uncomment this line: */
#define RTS_INVERT_LOGIC  // if defined, the RTS output logic is inverted

#ifdef RTS_INVERT_LOGIC
  #define RTS_HIGH LOW
  #define RTS_LOW HIGH
#else
  #define RTS_HIGH HIGH
  #define RTS_LOW LOW
#endif

#define RTS_PIN D3

/* Rx pin for SoftwareSerial */
#define RX_PIN RX

/* MQTT topic & InfluxDB column configuration */

typedef struct {
  char ident[12];
  int type;
  char influx_column[24];
  char mqtt_path[48];
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
// #define THREE_PHASE   // if defined, L2 and L3 metrics will be enabled

/* Metric and MQTT topic settings: comment a line to disable processing/publishing of the metric.
 * Leaving the third entry (influx_column) empty will disable inclusion in the influx line, if influx functionality is enabled */

metricDef metricDefs[] = {  // metric metadata and destination column/topic lookup table
//{ "OBIS ref.",    METRIC_TYPE_CONSTANT,   "influx col.",    "MQTT path",  "description (only for serial debug output)" },
  { "1-3:0.2.8",    METRIC_TYPE_META,       "",               "",           "SMR protocol version"  },
  { "0-0:1.0.0",    METRIC_TYPE_META,       "timestamp",      "",           "telegram timestamp"    },
  { "0-0:96.1.1",   METRIC_TYPE_META_TEXT,  "meter_sn",       "",           "meter serial number"   },

  { "1-0:1.8.1",    METRIC_TYPE_FLOAT,  "delivered_low",      "/energy/mains/reading/delivered/low",    "total delivered energy (low tariff)" },
  { "1-0:1.8.2",    METRIC_TYPE_FLOAT,  "delivered_high",     "/energy/mains/reading/delivered/high",   "total delivered energy (high tariff)" },
  { "1-0:2.8.1",    METRIC_TYPE_FLOAT,  "redelivered_low",    "/energy/mains/reading/redelivered/low",  "total redelivered (low tariff)" },
  { "1-0:2.8.2",    METRIC_TYPE_FLOAT,  "redelivered_high",   "/energy/mains/reading/redelivered/high", "total redelivered (high tariff)" },
  { "0-0:96.14.0",  METRIC_TYPE_BARE,   "tariff",             "/energy/mains/reading/tariff",           "tariff" },
  { "1-0:1.7.0",    METRIC_TYPE_FLOAT,  "P_total",            "/energy/mains/power/total",              "total power" },
  { "1-0:2.7.0",    METRIC_TYPE_FLOAT,  "P_total_redelivery", "/energy/mains/power/total_redelivery",   "total redelivery power" },
  { "1-0:21.7.0",   METRIC_TYPE_FLOAT,  "P_L1",               "/energy/mains/power/L1",                 "L1 power" },
  { "1-0:22.7.0",   METRIC_TYPE_FLOAT,  "P_L1_redelivery",    "/energy/mains/power/L1_redelivery",      "L1 redelivery power" },
  { "1-0:32.7.0",   METRIC_TYPE_FLOAT,  "V_L1",               "/energy/mains/voltage/L1",               "L1 voltage" },
  { "1-0:31.7.0",   METRIC_TYPE_FLOAT,  "I_L1",               "/energy/mains/current/L1",               "L1 current" },
  #ifdef THREE_PHASE
  { "1-0:41.7.0",   METRIC_TYPE_FLOAT,  "P_L2",               "/energy/mains/power/L2",                 "L2 power" },
  { "1-0:42.7.0",   METRIC_TYPE_FLOAT,  "P_L2_redelivery",    "/energy/mains/power/L2_redelivery",      "L2 redelivery power" },
  { "1-0:52.7.0",   METRIC_TYPE_FLOAT,  "V_L2",               "/energy/mains/voltage/L2",               "L2 voltage" },
  { "1-0:51.7.0",   METRIC_TYPE_FLOAT,  "I_L2",               "/energy/mains/current/L2",               "L2 current" },
  { "1-0:61.7.0",   METRIC_TYPE_FLOAT,  "P_L3",               "/energy/mains/power/L3",                 "L3 power" },
  { "1-0:62.7.0",   METRIC_TYPE_FLOAT,  "P_L3_redelivery",    "/energy/mains/power/L3_redelivery",      "L3 redelivery power" },
  { "1-0:72.7.0",   METRIC_TYPE_FLOAT,  "V_L3",               "/energy/mains/voltage/L3",               "L3 voltage" },
  { "1-0:71.7.0",   METRIC_TYPE_FLOAT,  "I_L3",               "/energy/mains/current/L3",               "L3 current" },
  #endif

  { "0-1:24.1.0",   METRIC_TYPE_BARE,       "",               "",   "gas meter device type" },
  { "0-1:96.1.0",   METRIC_TYPE_META_TEXT,  "gas_meter_sn",   "",   "gas meter serial number" },

  { "0-1:24.2.1",   METRIC_TYPE_GAS,    "gas_reading",    "/energy/gas/reading",   "gas meter last reading" },

  { "0-0:96.7.21",  METRIC_TYPE_BARE,   "failures",       "/energy/mains/report/powerfailures",         "power failures" },
  { "0-0:96.7.9",   METRIC_TYPE_BARE,   "long_failures",  "/energy/mains/report/powerfailures_long",    "long power failures" },
  { "1-0:99.97.0",  METRIC_TYPE_OTHER,  "failure_log",    "/energy/mains/report/powerfailure_details",  "power failure event log" },
  { "1-0:32.32.0",  METRIC_TYPE_BARE,   "L1_sags",        "/energy/mains/report/voltage_sags/L1",       "L1 voltage sags" },
  { "1-0:32.36.0",  METRIC_TYPE_BARE,   "L1_swells",      "/energy/mains/report/voltage_swells/L1",     "L1 voltage swells" },
  #ifdef THREE_PHASE
  { "1-0:52.32.0",  METRIC_TYPE_BARE,   "L2_sags",        "/energy/mains/report/voltage_sags/L2",       "L2 voltage sags" },
  { "1-0:52.36.0",  METRIC_TYPE_BARE,   "L2_swells",      "/energy/mains/report/voltage_swells/L2",     "L2 voltage swells" },
  { "1-0:72.32.0",  METRIC_TYPE_BARE,   "L3_sags",        "/energy/mains/report/voltage_sags/L3",       "L3 voltage sags" },
  { "1-0:72.36.0",  METRIC_TYPE_BARE,   "L3_swells",      "/energy/mains/report/voltage_swells/L3",     "L3 voltage swells" },
  #endif

  { "0-0:96.13.0",  METRIC_TYPE_TEXT,   "message",   "/energy/mains/message", "text message" }, // text message
  { "0-0:96.13.1",  METRIC_TYPE_TEXT,   "message",   "/energy/mains/message", "text message" }  // text message
};
