#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_BMP280.h>
#include <HTTPClient.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEAdvertising.h>
#include <Preferences.h>
#include <WiFiProv.h>
#include <esp_log.h>
#include <esp_wifi.h>

#define _DEBUG
#define BARO
#define _URL "https://gy91.almansa.ovh/api/record"    
//#define _URL "http://10.0.0.1/api/record"    

//BLECharacteristic *pCharacteristicSSID;
//BLECharacteristic *pCharacteristicPASS;

bool deviceConnected = false;
bool credentialsReceived = false;

void BLEScanTask(void * parameter);   //Tareas para FreeRTOS
void SensorTask(void * parameter);
void ini();                 // Inicializacion acelerometros
void WiFiStart();           // Inicialización de la WiFI
int registro();             // Registro de la MAC del dispositivo en el sistema 
void startBLEScan();        // Inicia el escaneo BLE
void enviar();              // Empaqueta y envía los datos por https
bool empaquetar(String& json);   
bool Api(const uint8_t* datos, size_t size);


HTTPClient apicall;       // Objeto cliente HTTP para llamar al API   
Adafruit_MPU6050 mpu;     // Objeto para el giroscopio/acelerómetro
Adafruit_BMP280 bmp;      // Objeto para el barómetro
extern volatile bool bleScanEnCurso ;

