//#ifndef BLESETUP_H
//#define BLESETUP_H

#include <WiFi.h>
#include <WiFiProv.h>
#include <Preferences.h>

const char *pop = "abcd1234";
const char *service_name = "PROV_ESP32";

Preferences preferences;

void setupBLEProvisioning() {
    Serial.begin(115200);
    delay(1000);
    #ifdef _DEBUG
        Serial.println("\n\n=== ESP32 BLE Provisioning ===\n");
    #endif

    // Verificar si ya hay credenciales guardadas manualmente
    preferences.begin("wifi", true);
    String savedSSID = preferences.getString("ssid", "");
    preferences.end();
    
    if (savedSSID.length() > 0) {
        Serial.println("✓ Credenciales encontradas, intentando conectar...");
        WiFi.begin();
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("✓ ¡Conectado!");
            Serial.println("IP: " + WiFi.localIP().toString());
            return;
        } else {
            Serial.println("✗ Fallo en conexión, reiniciando provisioning...");
            WiFi.disconnect();
        }
    }
    
    // Iniciar provisioning
    #ifdef _DEBUG
        Serial.println("Iniciando BLE provisioning...");
        Serial.println("Nombre: " + String(service_name));
        Serial.println("PoP: " + String(pop));
    #endif
    
    WiFiProv.beginProvision(
        WIFI_PROV_SCHEME_BLE,
        WIFI_PROV_SCHEME_HANDLER_FREE_BTDM,
        WIFI_PROV_SECURITY_1,
        pop,
        service_name
    );
}

bool waitForWiFi(int timeout_seconds = 30) {
    int attempts = 0;
    int max_attempts = timeout_seconds * 2;
    
    while (WiFi.status() != WL_CONNECTED && attempts < max_attempts) {
        delay(500);
        Serial.print(".");
        attempts++;
        
        // Verificar si el provisioning completó
        if (WiFi.status() == WL_CONNECTED) {
            #ifdef _DEBUG
                Serial.println();
                Serial.println("✓ ¡Conectado a WiFi!");
                Serial.println("SSID: " + WiFi.SSID());
                Serial.println("IP: " + WiFi.localIP().toString());
            #endif
            
            // Guardar credenciales para futuros reinicios
            preferences.begin("wifi", false);
            preferences.putString("ssid", WiFi.SSID());
            preferences.putString("pass", WiFi.psk());
            preferences.end();
            
            return true;
        }
    }
    
    return false;
}

//#endif