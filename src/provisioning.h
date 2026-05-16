

#define WiFiStatusPin 8     // 8-> Built in led
#define BOOT_BUTTON_PIN 9    // // Definición del botón BOOT en ESP32-C3

#define BOOT_HOLD_TIME 3000  // Mantener presionado 3 segundos para forzar provisioning

// Variables y handlers para FreeRTOS
enum WiFiStatusEnum
{
  WIFI_DISCONNECTED = 0,
  WIFI_PROVISIONING = 1,
  WIFI_CONNECTED = 2
};

volatile WiFiStatusEnum WiFiStatus = WIFI_DISCONNECTED;
SemaphoreHandle_t WiFiStatusMutex = NULL;
TaskHandle_t WiFiTestTaskHandle = NULL;
TaskHandle_t WiFiLedTaskHandle = NULL;
Preferences preferences;

const char* pop = "abcd1234";           // PoP para BLE provisioning
const char* service_key = NULL;
bool reset_provisioned = false;
bool provisioning_active = false;
String device_service_name;            // Nombre de servicio con MAC
void borrarCredenciales();
void WiFiStart();
void WiFiTest();
bool checkBootButton();
void startProvisioning();

void WiFiStart()            //////////  Recupera credenciales y trata de conectar
{
  #ifdef _DEBUG
    esp_log_level_set("*", ESP_LOG_DEBUG);
    esp_log_level_set("wifi", ESP_LOG_DEBUG);
    esp_log_level_set("provisioning", ESP_LOG_DEBUG);
  #endif

  WiFi.mode(WIFI_STA);
  
  String mac = WiFi.macAddress();
  device_service_name = "Pofenas_" + mac;
  Serial.println(mac);
  WiFi.disconnect();
  #ifdef _DEBUG
    Serial.println("Servicio de provisioning: " + device_service_name);
  #endif

  // Cargar credenciales guardadas
  preferences.begin("wifi", false);
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("pass", "");
  preferences.end();

  #ifdef _DEBUG
    Serial.println("SSID guardado: '" + ssid + "'");
    Serial.println("Pass length: " + String(pass.length()));
  #endif

  // Configurar el manejador de eventos de Wi-Fi
  WiFi.onEvent([](arduino_event_t* sys_event)
  {
    switch (sys_event->event_id)
    {
      case ARDUINO_EVENT_PROV_CRED_RECV:
      {
        preferences.begin("wifi", false);
        preferences.putString("ssid", (const char*)sys_event->event_info.prov_cred_recv.ssid);
        preferences.putString("pass", (const char*)sys_event->event_info.prov_cred_recv.password);
        preferences.end();
        #ifdef _DEBUG
          Serial.println("Credenciales recibidas y guardadas");
        #endif
        break;
      }
      case ARDUINO_EVENT_PROV_END:
        provisioning_active = false;
        #ifdef _DEBUG
          Serial.println("Provisioning finalizado");
        #endif
        break;

      case ARDUINO_EVENT_PROV_CRED_FAIL:
        #ifdef _DEBUG
          Serial.println("Error: Credenciales incorrectas");
        #endif
        break;

      case ARDUINO_EVENT_PROV_CRED_SUCCESS:
        #ifdef _DEBUG
          Serial.println("Credenciales verificadas correctamente");
        #endif
        break;
    }
  });

  // Intentar conectar con credenciales guardadas (máximo 3 intentos)
  int attempts = 0;
  bool connected = false;

  if (ssid.length() > 0 && pass.length() > 0)
  {
    while (attempts < 3 && !connected)
    {
      WiFi.begin(ssid.c_str(), pass.c_str());
      unsigned long startAttempt = millis();

      while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
        delay(100);
      }

      if (WiFi.status() == WL_CONNECTED)
      {
        Serial.println("Conectado a Wi-Fi");
        connected = true;
      } else {
        Serial.println("Intento de conexión fallido");
        attempts++;
        delay(2000);
      }
    }
  }

  // Si no se conectó, iniciar modo provisioning
  if (!connected)
  {
    #ifdef DEBUG
    Serial.println("Iniciando modo provisioning por fallo de conexión...");
    #endif
    startProvisioning();  // ← ya hace deinit, reset WiFi y beginProvision correctamente
  }
}
void WiFiTestC(void *parameter)  ////////// Tarea que monitorea el estado WiFi y el botón BOOT
{
  // Configurar el botón BOOT
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(500);

  bool lastButtonState = HIGH;
  unsigned long buttonPressTime = 0;
  const unsigned long LONG_PRESS_TIME = 3000;
  bool provisioningTriggered = false;

  WiFiStatusEnum lastStatus = (WiFiStatusEnum)-1; // Fuera de cualquier bloque condicional

  for(;;)
  {
    WiFiStatusEnum newStatus;
    #ifdef _DEBUG
    static unsigned long loopCount = 0;
    loopCount++;
    // Mostrar que está vivo cada 20 iteraciones (cada 10 segundos aprox)
    if (loopCount % 20 == 0) {
      Serial.print("[WiFiTestC] Vivo - Loop: ");
      Serial.print(loopCount);
      Serial.print(" - Estado: ");
      if (WiFi.status() == WL_CONNECTED) Serial.println("CONECTADO");
      else if (provisioning_active) Serial.println("PROVISIONING");
      else Serial.println("DESCONECTADO");
    }
  #endif

    // ===== CHEQUEO DEL BOTÓN BOOT =====
    bool currentButtonState = digitalRead(BOOT_BUTTON_PIN);

    if (currentButtonState == LOW && lastButtonState == HIGH) {
      // Botón recién presionado
      buttonPressTime = millis();
      provisioningTriggered = false;
      #ifdef _DEBUG
        Serial.println("[WiFiTestC] Botón BOOT presionado - mantén 3 seg");
      #endif
    }
    else if (currentButtonState == LOW && lastButtonState == LOW && !provisioningTriggered) {
      // Botón sigue presionado
      if (buttonPressTime > 0 && (millis() - buttonPressTime >= LONG_PRESS_TIME)) {
        #ifdef _DEBUG
          Serial.println("[WiFiTestC] ¡3 segundos! Iniciando provisioning...");
        #endif
        startProvisioning();
        provisioningTriggered = true;
      }
    }
    else if (currentButtonState == HIGH && lastButtonState == LOW) {
      // Botón liberado
      buttonPressTime = 0;
      provisioningTriggered = false;
    }

    lastButtonState = currentButtonState;

    // ===== CHEQUEO DEL ESTADO WIFI =====
    if (WiFi.status() == WL_CONNECTED) {
      newStatus = WIFI_CONNECTED;
    }
    else if (provisioning_active) {
      newStatus = WIFI_PROVISIONING;
    }
    else {
      newStatus = WIFI_DISCONNECTED;
    }

    // Actualizar la variable compartida
    if (WiFiStatusMutex != NULL) {
      if (xSemaphoreTake(WiFiStatusMutex, portMAX_DELAY) == pdTRUE) {
        WiFiStatus = newStatus;
        xSemaphoreGive(WiFiStatusMutex);
      }
    }

    #ifdef _DEBUG
      if (newStatus != lastStatus) {
        Serial.print("[WiFiTestC] Estado: ");
        if (newStatus == WIFI_CONNECTED) {
          Serial.println("CONECTADO");
        } else if (newStatus == WIFI_PROVISIONING) {
          Serial.println("PROVISIONING");
        } else {
          Serial.println("DESCONECTADO");
        }
        lastStatus = newStatus;
      }
    #endif

    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}
void WiFiLed(void *parameter)  ////////// Tarea que controla el LED según WiFiStatus
{
  pinMode(WiFiStatusPin, OUTPUT);
  digitalWrite(WiFiStatusPin, HIGH); // Apagado inicialmente

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(100); // Actualizar cada 100ms
  bool ledState = false;

  for(;;)
  {
    WiFiStatusEnum currentStatus;

    // Leer el estado de forma segura
    if (WiFiStatusMutex != NULL) {
      if (xSemaphoreTake(WiFiStatusMutex, portMAX_DELAY) == pdTRUE) {
        currentStatus = WiFiStatus;
        xSemaphoreGive(WiFiStatusMutex);
      }
    }

    // Controlar el LED según el estado
    switch (currentStatus) {
      case WIFI_CONNECTED:
        // LED encendido (pin LOW)
        digitalWrite(WiFiStatusPin, LOW);
        break;

      case WIFI_PROVISIONING:
        // LED parpadeando (alternar cada 100ms)
        ledState = !ledState;
        digitalWrite(WiFiStatusPin, ledState ? LOW : HIGH);
        break;

      case WIFI_DISCONNECTED:
        // LED apagado (pin HIGH)
        digitalWrite(WiFiStatusPin, HIGH);
        break;
    }

    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}
void initWiFiTasks()  ////////// Inicializa las tareas concurrentes de WiFi
{
  Serial.println("[initWiFiTasks] Iniciando...");

  // Crear el mutex
  WiFiStatusMutex = xSemaphoreCreateMutex();

  if (WiFiStatusMutex == NULL) 
  {
    Serial.println("[ERROR] No se pudo crear el mutex WiFiStatus");
    return;
  }
  Serial.println("[OK] Mutex creado");

  // Crear la tarea de monitoreo WiFi en Core 0
  BaseType_t xReturned = xTaskCreatePinnedToCore(
    WiFiTestC,              // Función de la tarea
    "WiFiTestTask",         // Nombre de la tarea
    4096,                   // Tamaño del stack (bytes)
    NULL,                   // Parámetros
    2,                      // Prioridad (2 = media)
    &WiFiTestTaskHandle,    // Handle de la tarea
    0                       // Core 0
  );

  if (xReturned != pdPASS) {
    Serial.println("[ERROR] No se pudo crear la tarea WiFiTestC");
    return;
  }
  Serial.println("[OK] Tarea WiFiTestC creada");

  // Crear la tarea del LED en Core 0
  xReturned = xTaskCreatePinnedToCore(
    WiFiLed,                // Función de la tarea
    "WiFiLedTask",          // Nombre de la tarea
    2048,                   // Tamaño del stack (bytes)
    NULL,                   // Parámetros
    1,                      // Prioridad (1 = baja, no es crítica)
    &WiFiLedTaskHandle,     // Handle de la tarea
    0                       // Core 0
  );

  if (xReturned != pdPASS) 
  {
    Serial.println("[ERROR] No se pudo crear la tarea WiFiLed");
    return;
  }
  Serial.println("[OK] Tarea WiFiLed creada");

  Serial.println("[OK] Tareas WiFi concurrentes inicializadas correctamente");
}
void startProvisioning()  ////////// Inicia el modo provisioning
{
   // Esperar a que termine el escaneo BLE si está en curso
    int espera = 0;
    while (bleScanEnCurso && espera < 20)
    {
        Serial.println("[PROV] Esperando fin de escaneo BLE...");
        delay(500);
        espera++;
    }
  #ifdef _DEBUG
    Serial.println("[startProvisioning] Iniciando...");
    Serial.print("provisioning_active actual: ");
    Serial.println(provisioning_active);
  #endif

  // Forzar a false para permitir reinicio
  provisioning_active = false;
  BLEDevice::deinit(false);
  // CRÍTICO: Desconectar WiFi completamente
  WiFi.disconnect(true, true);  // true,true = wifioff + erase AP
  delay(500);

  WiFi.mode(WIFI_OFF);
  delay(500);

  // Borrar credenciales de Preferences
  preferences.begin("wifi", false);
  preferences.clear();
  preferences.end();

  // Borrar también del NVS del sistema WiFi
  esp_wifi_restore();  // Resetea configuración WiFi a valores de fábrica
  delay(200);

  #ifdef _DEBUG
    Serial.println("WiFi reseteado completamente");
  #endif

  // Reiniciar WiFi en modo STA
  WiFi.mode(WIFI_STA);
  delay(200);

  #ifdef _DEBUG
    Serial.print("Iniciando BLE provisioning como: ");
    Serial.println(device_service_name);
  #endif

  // Iniciar provisioning BLE
  WiFiProv.beginProvision(
    WIFI_PROV_SCHEME_BLE,
    WIFI_PROV_SCHEME_HANDLER_FREE_BTDM,
    WIFI_PROV_SECURITY_1,
    pop,
    device_service_name.c_str(),
    service_key,
    NULL,
    false  // IMPORTANTE: false para permitir re-provisioning
  );

  provisioning_active = true;

  // Actualizar estado global
  if (WiFiStatusMutex != NULL) {
    if (xSemaphoreTake(WiFiStatusMutex, portMAX_DELAY) == pdTRUE) {
      WiFiStatus = WIFI_PROVISIONING;
      xSemaphoreGive(WiFiStatusMutex);
    }
  }

  #ifdef _DEBUG
    Serial.println("Provisioning BLE ACTIVADO - busca: " + device_service_name);
  #endif
}
