//https://docs.espressif.com/projects/esp-idf/en/latest/esp32/hw-reference/esp32/get-started-devkitc.html

// Moisture Sensor
// KeeYees Bodenfeuchtesensor v1.2
//
// Humidity Temperature Sensor: DHT11
// lib: https://github.com/adafruit/DHT-sensor-library
//
// JSON lib: https://github.com/bblanchon/ArduinoJson
//
// Pressure Temperature Sensor: BMP280
// lib: https://github.com/adafruit/Adafruit_BMP280_Library

#include <driver/adc.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "time.h"
#include <array>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_BMP280.h>



// Forward declarations
class AllLogData;
class DB;
String timeToPlotlyFmt(const time_t* t);


#define DHTTYPE DHT11  // DHT 11
#define DHTPIN 33      // Digital pin connected to the DHT sensor
DHT dht(DHTPIN, DHTTYPE);

// Pressure Temperature Sensor BMP280
Adafruit_BMP280 bmp;  // use I2C interface
Adafruit_Sensor* bmp_temp = bmp.getTemperatureSensor();
Adafruit_Sensor* bmp_pressure = bmp.getPressureSensor();

const String url = "http://keyval.store/v1/";

int minHeapFree = 250000;

void checkMinHeap(){
  const int heap = ESP.getFreeHeap();
  if(heap<minHeapFree){
    minHeapFree = heap;
  }
}

void setupNtpTime() {
  const char* ntpServer = "pool.ntp.org";
  const long gmtOffset_sec = 60 * 60;
  const int daylightOffset_sec = 3600;
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void setupPlantMoistureSensor() {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);  // 0 - 2600mv
}

void setupHumidityTempSensor() {
  dht.begin();
}

void setupWifi() {
  const char* ssid = "FRITZ!Box 6591 Cable FK";
  const char* password = "12345";
  WiFi.begin(ssid, password);
  Serial.println("\nConnecting");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }
  Serial.println("\nConnected to the WiFi network");
}

void setupBarometerSensor() {
  int status = bmp.begin(BMP280_ADDRESS_ALT, BMP280_CHIPID);

  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */

  bmp_temp->printSensorDetails();
}

// Unit hPa
float getPressure() {
  try {
    sensors_event_t temp_event, pressure_event;
    //bmp_temp->getEvent(&temp_event);
    bmp_pressure->getEvent(&pressure_event);
    return pressure_event.pressure;
  } catch (...) {
    return 0.0;
  }

  //Serial.print(F("Temperature = "));
  //Serial.print(temp_event.temperature);
  //Serial.println(" *C");

  //Serial.print(F("Pressure = "));
  //Serial.print(pressure_event.pressure);
  //Serial.println(" hPa");
}

float getBMPTemp() {
  try {
    sensors_event_t temp_event, pressure_event;
    bmp_temp->getEvent(&temp_event);
    return temp_event.temperature;
  } catch (...) {
    return 0.0;
  }
}

float getHumidity() {
  return dht.readHumidity();
}

// Celcius
float getTemperature() {
  return dht.readTemperature();
}

// Average 10 measurements to reduce noise
int getSoilMoisture() {
  int sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += adc1_get_raw(ADC1_CHANNEL_7);
  }
  return sum / 10;
}



// Plotly time format: yyyy-mm-dd HH:MM:SS.ssssss
String timeToPlotlyFmt(const time_t* t) {
  tm* timeinfo;
  timeinfo = localtime(t);
  char buffer[25];
  strftime(buffer, 25, "%Y-%m-%d %H:%M:%S", timeinfo);
  //Serial.println(buffer);
  String out = String(buffer);
  return out;
}

String getPlotlyTime() {
  tm timeinfo;
  const bool validTime = getLocalTime(&timeinfo);
  if (!validTime) {
    Serial.println("Datapoint invalid time!");
    return "";
  }
  char buffer[25];
  strftime(buffer, 25, "%Y-%m-%d %H:%M:%S", &timeinfo);
  String out = String(buffer);
  return out;
}

class DB {
public:

  // Reduce number of elements in string to fit maxElements.
  // Elements separated by ','
  static void reduceToFit(String& s, const int maxElements) {
    int elements = 0;
    for (int i = 0; i < s.length(); i++) {
      if (s[i] == ',') {
        elements++;
      }
    }
    Serial.print("Elements: ");
    Serial.println(elements);
    if (elements > maxElements) {
      // "123,456,789", shift = 3, len = 11
      int shift = -1;
      for (int i = 0; i < s.length(); i++) {
        if (s[i] == ',') {
          elements--;
        }
        if (elements < maxElements) {
          shift = i;
          break;
        }
      }
      for (int i = 0; i < s.length() - shift - 1; i++) {
        s[i] = s[i + shift + 1];
      }

      // Delete remaining characters by setting to whitespace and calling trim()
      for (int i = s.length() - shift - 1; i < s.length(); i++) {
        s[i] = ' ';
      }
      s.trim();
    }
  }

  static int append(const String key, const String toAppend, const int maxElements = 1000, const int retries = 10) {
    String value;
    bool success = false;
    int status;
    for (int i = 0; i < retries; i++) {
      status = get(key, value);
      if (status == 200) {
        success = true;
        break;
      }
    }
    if (!success) {
      Serial.print("append->get failed with status code: ");
      Serial.println(status);
      return status;
    }

    checkMinHeap();
    value = value + "," + toAppend;
    reduceToFit(value, maxElements);

    for (int i = 0; i < retries; i++) {
      status = set(key, value);
      if (status == 200) {
        success = true;
        break;
      }
    }
    if (!success) {
      Serial.print("append->get failed with status code: ");
      Serial.println(status);
      return status;
    }
    return 0;
  }

  static int get(const String key, String& out) {
    String request = url + key + "/get";
    HTTPClient http;
    http.begin(request);
    const int status = http.GET();
    out = http.getString();
    return status;
  }

  // Return HTTP response
  static int set(const String key, const String val) {
    String request = url + key;
    HTTPClient http;
    http.begin(request);
    return http.POST(val);
  }
};


void printSensors() {
  int moist = getSoilMoisture();
  Serial.print("Soil Moisture: ");
  Serial.print(moist);

  float temp = getTemperature();
  Serial.print(" Temp: ");
  Serial.print(temp);

  float hum = getHumidity();
  Serial.print(" Humidity: ");
  Serial.print(hum);

  float pressure = getPressure();
  Serial.print(" Pressure: ");
  Serial.print(pressure);

  float BMPTemp = getBMPTemp();
  Serial.print(" BMPTemp: ");
  Serial.print(BMPTemp);

  tm timeinfo;
  getLocalTime(&timeinfo);
  Serial.println(&timeinfo, " DateTime: %A, %B %d %Y %H:%M:%S");
}



const String htmlTemplate = R"(
<html>
<head>
	<script src='https://cdn.plot.ly/plotly-2.18.0.min.js'></script>
</head>

<body>
<center><h1>Steve's Plant</h1></center>
	<div id='temp'></div>
  <div id='humidity'></div>
  <div id='moisture'></div>
  <div id='pressure'></div>
  <div id='heap'></div>
  <div id='minheap'></div>
</body>

<script>

var time;

function makePlot(div, title, unit, data){
  var trace = {
    x: time,
    y: data,
    type: 'scatter'
  };

  var layout = {
    title: {
      text: title,
    },
    xaxis: {
      title: {
        text: 'Date-Time',
      },
    },
    yaxis: {
      title: {
        text: unit,
      }
    }
  };

  Plotly.newPlot(div, [trace], layout);
}

async function getTime(){
  await fetch("http://keyval.store/v1/planttime/get")
    .then((response) => response.text())
    .then((data) => time = data.split(","))
}

async function doTemp(){
  await fetch("http://keyval.store/v1/planttemp/get")
    .then((response) => response.text())
    .then((data) => data.split(","))
    .then((data) => makePlot('temp', 'Temperature', 'Celcius', data))
}

async function doHumid(){
  await fetch("http://keyval.store/v1/planthumid/get")
    .then((response) => response.text())
    .then((data) => data.split(","))
    .then((data) => makePlot('humidity', 'Humidity', 'Percent', data))
}

async function doPressure(){
  await fetch("http://keyval.store/v1/plantpressure/get")
    .then((response) => response.text())
    .then((data) => data.split(","))
    .then((data) => makePlot('pressure', 'Pressure', 'hPa', data))
}

async function doMoist(){
  await fetch("http://keyval.store/v1/plantmoist/get")
    .then((response) => response.text())
    .then((data) => data.split(","))
    .then((data) => makePlot('moisture', 'Soil Moisture', '???', data))
}

async function doHeap(){
  await fetch("http://keyval.store/v1/plantheap/get")
    .then((response) => response.text())
    .then((data) => data.split(","))
    .then((data) => makePlot('heap', 'ESP Free Heap', 'bytes', data))
}

async function doMinHeap(){
  await fetch("http://keyval.store/v1/plantminheap/get")
    .then((response) => response.text())
    .then((data) => data.split(","))
    .then((data) => makePlot('minheap', 'ESP Minimum Free Heap', 'bytes', data))
}

async function main(){
  console.log("Starting");
  await getTime();
  console.log("Got time");
  console.log(time);
  console.log("Making plots");
  await doTemp();
  await doHumid();
  await doPressure();
  await doMoist();
  await doHeap();
  await doMinHeap();
  console.log("Done");  
}

main();

</script>
</html>
)";




class LogDataPoint {
public:
  time_t t;
  unsigned int heap;
  short temp;
  short moisture;
  float pressure;
  unsigned char humidity;

  void print() {
    Serial.print("Soil Moisture: ");
    Serial.print(moisture);

    Serial.print(" Temp: ");
    Serial.print(temp);

    Serial.print(" Humidity: ");
    Serial.print(humidity);

    Serial.print(" Pressure: ");
    Serial.print(pressure);

    Serial.print(" Heap: ");
    Serial.print(heap);

    Serial.print(" DateTime: ");
    const String datetime = timeToPlotlyFmt(&t);
    Serial.println(datetime);
  }
};

class AllLogData {
public:

  void setup() {
    // Reset all logged data
    // DB::set("planttime", "");
    // DB::set("planttemp", "");
    // DB::set("planthumid", "");
    // DB::set("plantpressure", "");
    // DB::set("plantmoist", "");
    // DB::set("plantheap", "");
    // DB::set("plantminheap", "");
  }

  void sendDataPoint() {
    String time = getPlotlyTime();
    String temp = String(getTemperature());
    String humid = String(getHumidity());
    String pressure = String(getPressure());
    String moist = String(getSoilMoisture());
    String heap = String(ESP.getFreeHeap());
    String minHeap = String(minHeapFree);

    Serial.print("Soil Moisture: ");
    Serial.print(moist);

    Serial.print(" Temp: ");
    Serial.print(temp);

    Serial.print(" Humidity: ");
    Serial.print(humid);

    Serial.print(" Pressure: ");
    Serial.print(pressure);

    Serial.print(" Heap: ");
    Serial.print(heap);

    Serial.print(" DateTime: ");
    Serial.println(time);


    DB::append("planttime", time);
    DB::append("planttemp", temp);
    DB::append("planthumid", humid);
    DB::append("plantpressure", pressure);
    DB::append("plantmoist", moist);
    DB::append("plantheap", heap);
    DB::append("plantminheap", minHeap);
  }
};

AllLogData logData;


void setup() {
  Serial.begin(9600);
  Serial.println("\n\nStartup");

  setupWifi();
  setupPlantMoistureSensor();
  setupHumidityTempSensor();
  setupNtpTime();
  setupBarometerSensor();
  logData.setup();
  DB::set("plant", htmlTemplate);
}

void loop() {
  logData.sendDataPoint();
  const int fiveMinutes = 1000 * 60 * 5;
  delay(fiveMinutes);
  //delay(5000);
}
