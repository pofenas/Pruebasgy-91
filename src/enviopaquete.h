
// Conectamos SCL a GPIO4
//            SDA a GPIO3

#define AT   1      // treshold para aceleracion
#define GT   1      // treshold para giroscopio
#define PT  35         // treshold para barómetro 
// Variables globales para almacenamiento
#define MAX_DATA_BUFFER 400  // Tamaño del buffer
#define MAX_ITAGS 4          // Máximo de iTAGs a trackear



#define ID_DEVICE 1     // TODO: consultar a la base de datos en el setup para obtener el id del dispositivo a partir de la MAC

struct SensorData {
    float ax, ay, az;
    float gx, gy, gz; 
    float p;
}; 

// Estructura para iTAGs con MAC y RSSI
struct ITagData 
{
    String mac;
    int rssi;
};

ITagData itags[MAX_ITAGS];
int itagCount = 0;
SensorData dataBuffer[MAX_DATA_BUFFER];
int bufferIndex = 0;
bool bufferReady = false;
volatile bool bleScanEnCurso = false;



float ax = 0,         // inicialización de los valores de los inerciales
      ay = 0,
      az = 0,
      gx = 0,
      gy = 0,
      gz = 0,
      p  = 0;


class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks
{
    void onResult(BLEAdvertisedDevice advertisedDevice) 
    {
        std::string deviceName = advertisedDevice.getName();
        
        if (deviceName == "iTAG") 
        {
            std::string mac = advertisedDevice.getAddress().toString();
            String macStr = String(mac.c_str());
            int rssi = advertisedDevice.getRSSI();
            
            // Verificar si ya existe en el array
            bool exists = false;
            for(int i = 0; i < itagCount; i++) {
                if(itags[i].mac == macStr) {
                    exists = true;
                    // Actualizar RSSI si es más fuerte
                    if(rssi != itags[i].rssi) {
                        itags[i].rssi = rssi;
                    }
                    break;
                }
            }
            
            // Si no existe y hay espacio, agregarlo
            if(!exists && itagCount < MAX_ITAGS) {
                itags[itagCount].mac = macStr;
                itags[itagCount].rssi = rssi;
                itagCount++;
                
                Serial.printf("✅ iTAG agregado: %s, RSSI: %d, Total: %d\n", 
                             macStr.c_str(), rssi, itagCount);
            } else if (exists) {
                Serial.printf("🔄 iTAG actualizado: %s, RSSI: %d\n", 
                             macStr.c_str(), rssi);
            }
            
            Serial.printf("Dispositivo BLE iTAG: %s, RSSI: %d\n", mac.c_str(), rssi);
        }
    }
};
void setup() 
{
  #ifdef _DEBUG
    Serial.begin(115200);     // Serial port
  #endif    
  ini();                      // Inicialización de giroscopio y barómetro
  WiFiStart();                // Inicialización de WiFi
  initWiFiTasks();            // Inicialización de las tareas concurrentes que se ocupan del provisioning. Importante lanzarlas antes que 
                              // las tareas que manejan BLE, porque si se inicia el provisioning se necesita disponer del BLE
  // Crear tarea de sensores
  xTaskCreatePinnedToCore(  // FreeRTOS: esta función se lanza de manera concurrente
        SensorTask,         // Funcion a ejecutar. Debe estar declarada void functionName(void *parameter)
        "SensorTask",       // Nombre de la tarea
        4096,               // Tamaño del stack
        NULL,               // Puntero a parámetros que se pasan a la función. Podría ser: (void*)&miVariable
        2,                  // Prioridad alta para sensores (de 0 a 3)
        NULL,               // Handle. Para referenciar la tarea después (por si se necesita pausar, reiniciar, etc)
        1                   // Núcleo. Esp32 tiene dos nucleos (0 y 1)
    );
  // Crear tarea BLE
  xTaskCreatePinnedToCore(
        BLEScanTask, 
        "BLEScanTask",
        16384,
        NULL,
        1,  // Prioridad media para BLE
        NULL,
        0   // Núcleo diferente si es posible
    );
    
    
}
void loop() 
{
 delay(100); // dummy. Parece que es importante que loop haga algo
}
void ini()
{
  Serial.println("Iniciando...");
  Wire.begin(3,4);
  delay(500);
  if (!mpu.begin()) 
    #ifdef _DEBUG
      Serial.println("¡Fallo MPU6050! Verifica conexiones.");
    #endif
  else
    #ifdef _DEBUG
      Serial.println("MPU6050 OK");
    #endif
  #ifdef BARO                                 // Si no hemos definido BARO (barómetro) se salta esta secciòn.
    if (!bmp.begin(0x76)) 
      #ifdef _DEBUG
        Serial.println("¡Fallo BMP280! Verifica conexiones.");
      #endif
    else
      #ifdef _DEBUG
        Serial.println("BMP280 OK");
      #endif
    #endif
}
void SensorTask(void * parameter) 
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = 100; // 100ms = 10Hz
    float presion;
    for(;;) 
     {
        // Leer y mostrar datos de sensores
        sensors_event_t a, g, temp;
        mpu.getEvent(&a, &g, &temp);
        presion = bmp.readPressure();
        if (
                 abs(a.acceleration.x - ax) > AT
              or abs(a.acceleration.y - ay) > AT 
              or abs(a.acceleration.z - az) > AT 
              or abs(g.gyro.x - gx) > GT
              or abs(g.gyro.y - gy) > GT
              or abs(g.gyro.z - gz) > GT
              or abs(presion - p)   > PT
            )
          {
              ax =  a.acceleration.x;
              ay =  a.acceleration.y;
              az =  a.acceleration.z;
              gx =  g.gyro.x;
              gy =  g.gyro.y;
              gz =  g.gyro.z;
              p  =  presion;
              if (bufferIndex < MAX_DATA_BUFFER) 
              {
                dataBuffer[bufferIndex] = {
                    ax, ay, az,
                    gx, gy, gz,
                    p
                };
                bufferIndex++;
                
                #ifdef _DEBUG
                    Serial.printf("📥 Dato almacenado en buffer [%d/%d]\n", 
                                 bufferIndex, MAX_DATA_BUFFER);
                #endif
              } 
                else 
              {
                #ifdef _DEBUG
                   // Serial.println("⚠️ Buffer lleno - dato perdido");
                #endif
              }
          }
     }
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
    
}
// Tarea BLE (escaneo periódico)
void BLEScanTask(void * parameter) 
{
    #ifdef _DEBUG
        Serial.println("[BLE] Tarea iniciada");
    #endif
    
    // Esperar a que termine el provisioning inicial (si existe)
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
    
    for(;;) 
    {
        WiFiStatusEnum currentStatus;
        if (WiFiStatusMutex != NULL && xSemaphoreTake(WiFiStatusMutex, 100 / portTICK_PERIOD_MS) == pdTRUE) 
        {
            currentStatus = WiFiStatus;
            xSemaphoreGive(WiFiStatusMutex);
        }
        
        if (currentStatus == WIFI_PROVISIONING && bleActive) 
        {
            #ifdef _DEBUG
                Serial.println("[BLE] Desinicializando para provisioning manual...");
            #endif
            BLEDevice::deinit(false);
            bleActive = false;
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        
        // Si sale de provisioning, ESPERAR antes de reinicializar BLE
        if (currentStatus != WIFI_PROVISIONING && !bleActive) 
        {
            #ifdef _DEBUG
                Serial.println("[BLE] Provisioning completado, esperando 5 seg antes de reiniciar BLE...");
            #endif
            vTaskDelay(5000 / portTICK_PERIOD_MS);  // ← CAMBIO AQUÍ: 5 segundos
            
            #ifdef _DEBUG
                Serial.println("[BLE] Reinicializando BLE para escaneo...");
            #endif
            BLEDevice::init("");
            bleActive = true;
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
        
        if (currentStatus != WIFI_PROVISIONING && bleActive) 
        {
            startBLEScan();
            if (bufferIndex > 0 || itagCount > 0) {
                enviar();
            }
            vTaskDelay(10000 / portTICK_PERIOD_MS);
        } else {
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
    }
}
void startBLEScan()
{
    // No escanear si hay provisioning activo
    if (provisioning_active)
        return;

    bleScanEnCurso = true;

    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(99);
    pBLEScan->setWindow(98);
    BLEScanResults foundDevices = pBLEScan->start(6, false);

    #ifdef DEBUG
    Serial.printf("Escaneo BLE finalizado. Dispositivos: %d\n", foundDevices.getCount());
    #endif

    pBLEScan->clearResults();
    bleScanEnCurso = false;
}
void enviar()   // Empaquetado y envío de los datos a la API
{
    //if (bufferIndex == 0 && itagCount == 0) return;
    
    String json;
    if (!empaquetar(json)) 
    {
        Serial.println("❌ Error construyendo JSON");
        return;
    }
    
    // Se pasan los datos JSON a la API
    if (!Api((uint8_t*)json.c_str(), json.length())) 
    {
        Serial.println("❌ Error enviando a API");
        return;
    }
    
    // Limpiar buffers después del envío exitoso
    bufferIndex = 0;
    for(int i = 0; i < MAX_ITAGS; i++) // limpiamos el buffer de iTAGs
        {
            itags[i].mac = "";
            itags[i].rssi = 0;
        }
    itagCount = 0;  // Limpiar iTAGs para el próximo escaneo
    
    #ifdef _DEBUG
        Serial.println("✅ Buffers limpiados para próximo envío");
    #endif
}
bool empaquetar(String& json)
{
    #ifdef _DEBUG
        Serial.println("🏗️ Construyendo JSON...");
    #endif
    
    json = "{\"sensor_data\":[";
    
    for(int i = 0; i < bufferIndex; i++) 
    {
        if(i > 0) json += ",";
        
        json += "{";
        json += "\"ax\":" + String((int)(dataBuffer[i].ax * 100)) + ",";
        json += "\"ay\":" + String((int)(dataBuffer[i].ay * 100)) + ","; 
        json += "\"az\":" + String((int)(dataBuffer[i].az * 100)) + ",";
        json += "\"gx\":" + String((int)(dataBuffer[i].gx * 100)) + ",";
        json += "\"gy\":" + String((int)(dataBuffer[i].gy * 100)) + ",";
        json += "\"gz\":" + String((int)(dataBuffer[i].gz * 100)) + ",";
        json += "\"presion\":" + String((int)(dataBuffer[i].p * 100)) + ",";
        json += "\"id_device\":1" ;
        json += "}";
    }
    
    json += "],\"itags\":[";
      Serial.println(json);
    // Agregar iTAGs al JSON con MAC y RSSI
    for(int i = 0; i < itagCount; i++) 
    {
        if(i > 0) json += ",";
        json += "{\"mac\":\"" + itags[i].mac + "\",\"rssi\":" + String(itags[i].rssi) + ",\"id_device\":" + ID_DEVICE + "}";
    }
    
    json += "]}";
    
    #ifdef _DEBUG
        Serial.printf("📦 JSON construido: %d bytes para %d lecturas y %d iTAGs\n", 
                     json.length(), bufferIndex, itagCount);
        Serial.printf("📦 Tamaño final: %d bytes\n", json.length());
        
        // Debug de iTAGs
        for(int i = 0; i < itagCount; i++) 
        {
            Serial.printf("📱 iTAG %d: %s (RSSI: %d)\n", i+1, itags[i].mac.c_str(), itags[i].rssi);
        }
    #endif
    
    return true;
}
bool Api(const uint8_t* datos, size_t size) // Envío de datos
{
    #ifdef _DEBUG
        Serial.printf("📤 Enviando %d bytes a API\n", size);
    #endif
    
    HTTPClient http;
    http.begin(_URL);
    http.addHeader("Content-Type", "application/msgpack");
    
    int httpCode = http.POST((uint8_t*) datos, size);
    
    #ifdef _DEBUG
        Serial.printf("📨 Código HTTP: %d\n", httpCode);
    #endif
    
    bool exito = (httpCode == 200);
    
    if (exito) 
      {
        String respuesta = http.getString();
        Serial.printf("✅ API: %s\n", respuesta.c_str());
      } 
    else 
      {
        Serial.printf("Error %d en el envío de datos\n", httpCode);
      }
    return exito;
  }

