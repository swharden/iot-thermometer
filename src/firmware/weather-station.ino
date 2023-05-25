#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Wire.h>

#define DEBUG_SHOW_BYTES false
#define DEVICE_ADDRESS 0x76
#define LED_PRIMARY 2     // D4
#define LED_SECONDARY 16  // D0

const char* ssid = "MY_SSID";
const char* password = "MY_SECRET_PASSWORD";

void setup() {
  Serial.begin(9600);
  delay(10);

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println(WiFi.localIP());

  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  pinMode(LED_PRIMARY, OUTPUT);
  pinMode(LED_SECONDARY, OUTPUT);

  Wire.begin(D1, D2);
}

void loop() {
  digitalWrite(LED_PRIMARY, LOW);

  makeMeasurement();

  uint32_t temp = getTemperature();
  float tempF = convertTemperature(temp);
  uint32_t pres = getPressure();
  uint32_t presConv = convertPressure(pres);
  float presPa = presConv / 256.0;
  float presPSI = presPa / 6894.76;

  make_request(tempF, presPSI);
  delay(500);

  digitalWrite(LED_PRIMARY, HIGH);
  wait(60);
}

void wait(int seconds) {
  while (seconds--) {
    digitalWrite(LED_SECONDARY, LOW);
    delay(2);
    digitalWrite(LED_SECONDARY, HIGH);
    delay(1000);
  }
}

void make_request(float temp, float pres) {

  String resource = String("https://swharden.com/weather/v1/write/");

  String data = String("{")
                + String("\"key\": \"MY_SECRET_API_KEY\",")
                + String("\"sensor\": \"2\",")
                + String("\"temperature\": \"") + String(temp) + String("\",")
                + String("\"pressure\": \"") + String(pres) + String("\"")
                + String("}");

  if ((WiFi.status() != WL_CONNECTED)) {
    Serial.printf("[HTTP] not connected");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();  // WARNING
  const int HTTPS_PORT = 443;
  client.connect("swharden.com", HTTPS_PORT);

  HTTPClient http;

  Serial.print("[HTTP] begin...\n");
  http.begin(client, resource);
  http.addHeader("Content-Type", "application/json");

  Serial.print("[HTTP] POST:");
  Serial.println(data);
  int httpCode = http.POST(data);

  String payload = http.getString();
  Serial.printf("[HTTP] Response (code: %d): ", httpCode);
  Serial.println(payload);

  http.end();
}

int32_t t_fine;
uint16_t dig_T1;
int16_t dig_T2;
int16_t dig_T3;

uint16_t dig_P1;
int16_t dig_P2;
int16_t dig_P3;
int16_t dig_P4;
int16_t dig_P5;
int16_t dig_P6;
int16_t dig_P7;
int16_t dig_P8;
int16_t dig_P9;

void makeMeasurement() {
  digitalWrite(LED_BUILTIN, LOW);
  readCalibrationData();
  setupConfigRegister();
  setupControlRegister();
  waitForMeasurement();
  digitalWrite(LED_BUILTIN, HIGH);
}

void readCalibrationData() {
  dig_T1 = ReadInt16_LE(0x88);
  dig_T2 = ReadInt16_LE(0x8A);
  dig_T3 = ReadInt16_LE(0x8C);
  dig_P1 = ReadInt16_LE(0x8E);
  dig_P2 = ReadInt16_LE(0x90);
  dig_P3 = ReadInt16_LE(0x92);
  dig_P4 = ReadInt16_LE(0x94);
  dig_P5 = ReadInt16_LE(0x96);
  dig_P6 = ReadInt16_LE(0x98);
  dig_P7 = ReadInt16_LE(0x9A);
  dig_P8 = ReadInt16_LE(0x9C);
  dig_P9 = ReadInt16_LE(0x9E);
}

void waitForMeasurement() {
  while (true) {
    Wire.beginTransmission(DEVICE_ADDRESS);
    Wire.write(0xF3);
    Wire.write(0xF3);
    Wire.endTransmission();
    byte status = Wire.read();
    if (status & 0b00001000)
      return;
  }
}

void setupConfigRegister() {
  byte config = 0;
  config |= 0b00000000;  // t_sb (0.5ms)
  config |= 0b00000000;  // filter (disable filter)
  config |= 0b00000000;  // spi3w_en (disable 3-wire)

  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(0xF5);
  Wire.write(config);
  Wire.endTransmission();
}

void setupControlRegister() {
  byte ctrl = 0;
  ctrl |= 0b01100000;  // set temperature resolution
  ctrl |= 0b00001100;  // set pressure resolution
  ctrl |= 0b00000011;  // mode (normal)

  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(0xF4);
  Wire.write(ctrl);
  Wire.endTransmission();
}

int16_t ReadInt16_LE(byte address) {
  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(address);
  Wire.endTransmission();

  Wire.requestFrom(DEVICE_ADDRESS, 3);
  byte lsb = Wire.read();
  byte msb = Wire.read();

  if (DEBUG_SHOW_BYTES) {
    Serial.print("bytes at ");
    Serial.print(address);
    Serial.print(" are ");
    Serial.print(lsb);
    Serial.print(", ");
    Serial.print(msb);
    Serial.println();
  }

  return (msb << 8) | (lsb);
}

uint32_t getPressure() {
  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(0xF7);
  Wire.endTransmission();

  Wire.requestFrom(DEVICE_ADDRESS, 3);
  byte press_msb = Wire.read();
  byte press_lsb = Wire.read();
  byte press_xlsb = Wire.read();

  if (DEBUG_SHOW_BYTES) {
    Serial.print("Pressure bytes: ");
    Serial.print(press_msb);
    Serial.print(", ");
    Serial.print(press_lsb);
    Serial.print(", ");
    Serial.print(press_xlsb);
    Serial.println();
  }

  uint32_t result;
  result += (uint32_t)press_msb << 16;
  result += (uint32_t)press_lsb << 8;
  result += (uint32_t)press_xlsb;

  return result;
}

uint32_t getTemperature() {
  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(0xFA);
  Wire.endTransmission();

  Wire.requestFrom(DEVICE_ADDRESS, 3);
  byte temp_msb = Wire.read();
  byte temp_lsb = Wire.read();
  byte temp_xlsb = Wire.read();

  if (DEBUG_SHOW_BYTES) {
    Serial.print("Temperature bytes: ");
    Serial.print(temp_msb);
    Serial.print(", ");
    Serial.print(temp_lsb);
    Serial.print(", ");
    Serial.print(temp_xlsb);
    Serial.println();
  }

  uint32_t result;
  result += (uint32_t)temp_msb << 16;
  result += (uint32_t)temp_lsb << 8;
  result += (uint32_t)temp_xlsb;

  return result;
}

float convertTemperature(long adc_T) {
  adc_T >>= 4;
  int32_t var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
  int32_t var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
  t_fine = var1 + var2;
  float T = (t_fine * 5 + 128) >> 8;
  T = T * 9 / 5 + 3200;  // C to F
  return T / 100;
}

uint32_t convertPressure(int32_t adc_P) {
  adc_P >>= 4;
  int64_t var1, var2, p;
  var1 = ((int64_t)t_fine) - 128000;
  var2 = var1 * var1 * (int64_t)dig_P6;
  var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
  var2 = var2 + (((int64_t)dig_P4) << 35);
  var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) + ((var1 * (int64_t)dig_P2) << 12);
  var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dig_P1) >> 33;
  if (var1 == 0) return 0;
  p = 1048576 - adc_P;
  p = (((p << 31) - var2) * 3125) / var1;
  var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
  var2 = (((int64_t)dig_P8) * p) >> 19;
  p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);
  return (float)p;
}