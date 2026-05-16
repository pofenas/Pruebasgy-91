// enviounitario.h
// Envío unitario: cada lectura de sensor se envía inmediatamente,
// y las lecturas BLE se envían al finalizar cada scan.

#ifndef ENVIOUNITARIO_H
#define ENVIOUNITARIO_H

//#include "Pofenas.h"

#define AT 1    // threshold para aceleración
#define GT 1    // threshold para giroscopio
#define PT 35   // threshold para barómetro

#define MAXITAGS 4
#define IDDEVICE 1  // TODO: obtener de BD a partir de MAC

// ─── Estructuras ────────────────────────────────────────────────────────────

struct SensorData
{
    float ax, ay, az;
    float gx, gy, gz;
    float p;
};

struct ITagData
{
    String mac;
    int rssi;
};

// ─── Variables globales ──────────────────────────────────────────────────────

ITagData itags[MAXITAGS];
int itagCount = 0;
volatile bool bleScanEnCurso = false;

SemaphoreHandle_t itagsMutex = NULL;  // Protege itags[] entre SensorTask y BLEScanTask
SemaphoreHandle_t httpMutex = NULL;


float ax = 0, ay = 0, az = 0;
float gx = 0, gy = 0, gz = 0;
float p  = 0;

// ─── Callback BLE ────────────────────────────────────────────────────────────

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
    void onResult(BLEAdvertisedDevice advertisedDevice)
    {
        std::string deviceName = advertisedDevice.getName();
        if (deviceName == "iTAG")
        {
            std::string mac = advertisedDevice.getAddress().toString();
            String macStr   = String(mac.c_str());
            int rssi        = advertisedDevice.getRSSI();

            if (itagsMutex != NULL)
            {
                xSemaphoreTake(itagsMutex, portMAX_DELAY);
            }

            bool exists = false;
            for (int i = 0; i < itagCount; i++)
            {
                if (itags[i].mac == macStr)
                {
                    exists = true;
                    if (rssi != itags[i].rssi)
                    {
                        itags[i].rssi = rssi;
                    }
                    break;
                }
            }
            if (!exists && itagCount < MAXITAGS)
            {
                itags[itagCount].mac  = macStr;
                itags[itagCount].rssi = rssi;
                itagCount++;
                #ifdef _DEBUG
                Serial.printf("[iTAG] Agregado: %s, RSSI: %d, Total: %d\n",
                              macStr.c_str(), rssi, itagCount);
                #endif
            }

            if (itagsMutex != NULL)
            {
                xSemaphoreGive(itagsMutex);
            }
        }
    }
};

// ─── Prototipos ──────────────────────────────────────────────────────────────

void ini();
void WiFiStart();
void SensorTask(void* parameter);
void BLEScanTask(void* parameter);
void startBLEScan();
bool enviarSensor(SensorData dato);
bool enviarBLE();
bool buildAndSend(String sensorJson, String itagsJson);
bool Api(const uint8_t* datos, size_t size);

// ─── Setup ───────────────────────────────────────────────────────────────────

void setup()
{

    #ifdef _DEBUG
    Serial.begin(115200);
    Serial.println("Iniciando");
    #endif

  
    ini();
    
    WiFiStart();
    initWiFiTasks();

    itagsMutex = xSemaphoreCreateMutex();
    httpMutex = xSemaphoreCreateMutex();


    // Tarea de sensores — Core 1, prioridad alta
    xTaskCreatePinnedToCore(
        SensorTask,
        "SensorTask",
        24567,
        NULL,
        2,
        NULL,
        1
    );

    // Tarea BLE — Core 0, prioridad media
    xTaskCreatePinnedToCore(
        BLEScanTask,
        "BLEScanTask",
        16384,
        NULL,
        1,
        NULL,
        0
    );
}

void loop()
{
    delay(100);
}

// ─── Inicialización sensores ─────────────────────────────────────────────────

void ini()
{
    Serial.println("Iniciando...");
    Wire.begin(3, 4);
    delay(500);

    if (!mpu.begin())
    {
        #ifdef _DEBUG
        Serial.println("Fallo MPU6050! Verifica conexiones.");
        #endif
    }
    else
    {
        #ifdef _DEBUG
        Serial.println("MPU6050 OK");
        #endif
    }

    #ifdef BARO
    if (!bmp.begin(0x76))
    {
        #ifdef _DEBUG
        Serial.println("Fallo BMP280! Verifica conexiones.");
        #endif
    }
    else
    {
        #ifdef _DEBUG
        Serial.println("BMP280 OK");
        #endif
    }
    #endif
}

// ─── Tarea sensores ──────────────────────────────────────────────────────────

void SensorTask(void* parameter)
{
    // Esperar WiFi antes de poder enviar
    while (WiFi.status() != WL_CONNECTED)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

     unsigned long ultimoEnvio = 0;
     float presion = 0;


    for (;;)
    {
        sensors_event_t a, g, temp;
        mpu.getEvent(&a, &g, &temp);

        #ifdef BARO
        presion = bmp.readPressure();
        #endif

        bool superaUmbral =
            abs(a.acceleration.x - ax) > AT ||
            abs(a.acceleration.y - ay) > AT ||
            abs(a.acceleration.z - az) > AT ||
            abs(g.gyro.x - gx)         > GT ||
            abs(g.gyro.y - gy)         > GT ||
            abs(g.gyro.z - gz)         > GT ||
            abs(presion  - p)          > PT;

        if (superaUmbral)
        {
            ax = a.acceleration.x;
            ay = a.acceleration.y;
            az = a.acceleration.z;
            gx = g.gyro.x;
            gy = g.gyro.y;
            gz = g.gyro.z;
            p  = presion;

            // Rate limiting: mínimo 500ms entre envíos
            unsigned long ahora = millis();
            if (ahora - ultimoEnvio >= 500)
            {
                SensorData dato = {ax, ay, az, gx, gy, gz, p};
                if (enviarSensor(dato))
                {
                    ultimoEnvio = ahora;
                }
            }
        }

          vTaskDelay(100 / portTICK_PERIOD_MS);  // ← vTaskDelay en lugar de vTaskDelayUntil
    }
}

// ─── Tarea BLE ────────────────────────────────────────────────────────────────

void BLEScanTask(void* parameter)
{
    #ifdef _DEBUG
    Serial.println("[BLE] Tarea iniciada");
    #endif

    while (WiFi.status() != WL_CONNECTED)
    {
        #ifdef _DEBUG
        Serial.println("[BLE] Esperando WiFi conectado antes de iniciar BLE...");
        #endif
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    #ifdef _DEBUG
    Serial.println("[BLE] WiFi conectado, inicializando BLE...");
    #endif

    BLEDevice::init("");
    bool bleActive = true;

    for (;;)
    {
        WiFiStatusEnum currentStatus = WIFI_DISCONNECTED;
        if (WiFiStatusMutex != NULL)
        {
            if (xSemaphoreTake(WiFiStatusMutex, 100 / portTICK_PERIOD_MS) == pdTRUE)
            {
                currentStatus = WiFiStatus;
                xSemaphoreGive(WiFiStatusMutex);
            }
        }

        // Si entra en provisioning, liberar BLE
        if (currentStatus == WIFI_PROVISIONING && bleActive)
        {
            #ifdef _DEBUG
            Serial.println("[BLE] Desinicializando para provisioning...");
            #endif
            BLEDevice::deinit(false);
            bleActive = false;
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        // Si sale de provisioning, reinicializar BLE
        if (currentStatus != WIFI_PROVISIONING && !bleActive)
        {
            #ifdef _DEBUG
            Serial.println("[BLE] Provisioning completado, esperando 5s antes de reiniciar BLE...");
            #endif
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            #ifdef _DEBUG
            Serial.println("[BLE] Reinicializando BLE...");
            #endif
            BLEDevice::init("");
            bleActive = true;
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }

        if (currentStatus != WIFI_PROVISIONING && bleActive)
        {
            startBLEScan();   // Escanea 6 segundos
            enviarBLE();      // Envía lo encontrado
        }
        else
        {
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
    }
}

// ─── Escaneo BLE ─────────────────────────────────────────────────────────────

void startBLEScan()
{
    if (provisioning_active)
        return;

    #ifdef _DEBUG
    Serial.println("Iniciando escaneo BLE...");
    #endif

    bleScanEnCurso = true;

    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(99);
    pBLEScan->setWindow(98);
    BLEScanResults foundDevices = pBLEScan->start(6, false);

    #ifdef _DEBUG
    Serial.printf("Escaneo BLE finalizado. Dispositivos: %d\n", foundDevices.getCount());
    #endif

    pBLEScan->clearResults();
    bleScanEnCurso = false;
}

// ─── Envío sensor (un único registro) ────────────────────────────────────────

bool enviarSensor(SensorData dato)
{
    String sensorJson = "[{";
    sensorJson += "\"ax\":"      + String((int)(dato.ax * 100)) + ",";
    sensorJson += "\"ay\":"      + String((int)(dato.ay * 100)) + ",";
    sensorJson += "\"az\":"      + String((int)(dato.az * 100)) + ",";
    sensorJson += "\"gx\":"      + String((int)(dato.gx * 100)) + ",";
    sensorJson += "\"gy\":"      + String((int)(dato.gy * 100)) + ",";
    sensorJson += "\"gz\":"      + String((int)(dato.gz * 100)) + ",";
    sensorJson += "\"presion\":" + String((int)(dato.p  * 100)) + ",";
    sensorJson += "\"id_device\":" + String(IDDEVICE);
    sensorJson += "}]";

    return buildAndSend(sensorJson, "[]");
}

// ─── Envío BLE (iTAGs del último scan) ───────────────────────────────────────

bool enviarBLE()
{
    if (itagsMutex != NULL)
    {
        xSemaphoreTake(itagsMutex, portMAX_DELAY);
    }

    if (itagCount == 0)
    {
        if (itagsMutex != NULL)
        {
            xSemaphoreGive(itagsMutex);
        }
        return true;  // Nada que enviar, no es un error
    }

    String itagsJson = "[";
    for (int i = 0; i < itagCount; i++)
    {
        if (i > 0) itagsJson += ",";
        itagsJson += "{";
        itagsJson += "\"mac\":\""    + itags[i].mac + "\",";
        itagsJson += "\"rssi\":"     + String(itags[i].rssi) + ",";
        itagsJson += "\"id_device\":" + String(IDDEVICE);
        itagsJson += "}";
    }
    itagsJson += "]";

    // Limpiar iTAGs para el próximo scan
    for (int i = 0; i < MAXITAGS; i++)
    {
        itags[i].mac  = "";
        itags[i].rssi = 0;
    }
    itagCount = 0;

    if (itagsMutex != NULL)
    {
        xSemaphoreGive(itagsMutex);
    }

    return buildAndSend("[]", itagsJson);
}

// ─── Construcción JSON y envío ────────────────────────────────────────────────

bool buildAndSend(String sensorJson, String itagsJson)
{
    String json = "{";
    json += "\"sensor_data\":"  + sensorJson + ",";
    json += "\"itags\":"        + itagsJson;
    json += "}";

    #ifdef _DEBUG
    Serial.printf("[API] Enviando %d bytes\n", json.length());
    #endif

    return Api((const uint8_t*)json.c_str(), json.length());
}

// ─── Llamada HTTP a la API ────────────────────────────────────────────────────

bool Api(const uint8_t* datos, size_t size)
{
    // Intentar tomar el mutex, pero solo esperar 50ms máximo
    if (httpMutex != NULL)
    {
        if (xSemaphoreTake(httpMutex, 50 / portTICK_PERIOD_MS) != pdTRUE)
        {
            #ifdef _DEBUG
            Serial.println("[API] HTTP ocupado, descartando envío");
            #endif
            return false;  // ← descartar en lugar de acumular
        }
    }

    HTTPClient http;
    http.begin(_URL);
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.POST((uint8_t*)datos, size);
    // ... resto igual
    http.end();

    if (httpMutex != NULL)
        xSemaphoreGive(httpMutex);

    return (httpCode == 200);
}



#endif // ENVIOUNITARIO_H
