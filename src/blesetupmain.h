#include <WiFi.h>
#include <WiFiProv.h>
#include <Preferences.h>

#define BLE_LED 8

Preferences prefs;
bool bleActive = false;
void checkConnection();
void startBLE();

// Handler
void eventHandler(arduino_event_t *e) 
{
    switch (e->event_id) {
        case ARDUINO_EVENT_PROV_START:
            Serial.println("BLE ACTIVE");
            bleActive = true;
            digitalWrite(BLE_LED, LOW);
            break;
            
        case ARDUINO_EVENT_PROV_END:
            Serial.println("BLE ENDED");
            bleActive = false;
            digitalWrite(BLE_LED, HIGH);
            
            // Save and restart
            if (WiFi.SSID() != "") {
                prefs.begin("wifi", false);
                prefs.putString("ssid", WiFi.SSID());
                prefs.putString("pass", WiFi.psk());
                prefs.end();
                delay(3000);
                ESP.restart();
            }
            break;
            
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.print("WIFI OK: ");
            Serial.println(WiFi.localIP());
            bleActive = false;
            digitalWrite(BLE_LED, HIGH);
            break;
            
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            checkConnection();
            break;
    }
}

void checkConnection() 
{
    if (bleActive) return;
    
    prefs.begin("wifi", true);
    String ssid = prefs.getString("ssid", "");
    prefs.end();
    
    if (ssid == "") {
        startBLE();
    } else {
        static int tries = 0;
        tries++;
        
        if (tries > 2) {
            tries = 0;
            startBLE();
        } else {
            WiFi.reconnect();
        }
    }
}

void startBLE() 
{
    Serial.println("STARTING BLE...");
    
    WiFi.disconnect(true, true);
    delay(1000);
    
    WiFi.onEvent(eventHandler);
    WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE, 
                           WIFI_PROV_SCHEME_HANDLER_FREE_BTDM);
    
    digitalWrite(BLE_LED, HIGH);
    bleActive = true;
}

void setup() {
    Serial.begin(115200);
    pinMode(BLE_LED, OUTPUT);
    digitalWrite(BLE_LED, LOW);
    
    delay(2000);
    Serial.println("\nESP32-C3 Auto WiFi/BLE\n");
    
    WiFi.onEvent(eventHandler);
    
    prefs.begin("wifi", true);
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    prefs.end();
    
    if (ssid != "") {
        Serial.print("Connecting to: ");
        Serial.println(ssid);
        WiFi.begin(ssid.c_str(), pass.c_str());
        
        int timeout = 0;
        while (WiFi.status() != WL_CONNECTED && timeout < 20) {
            delay(500);
            Serial.print(".");
            timeout++;
        }
        
        if (WiFi.status() != WL_CONNECTED) {
            checkConnection();
        }
    } else {
        startBLE();
    }
}

void loop() {
    if (!bleActive && WiFi.status() != WL_CONNECTED) {
        static unsigned long lastCheck = 0;
        if (millis() - lastCheck > 60000) {
            lastCheck = millis();
            checkConnection();
        }
    }
    
    if (bleActive) {
        // Blink LED when in BLE mode
        static unsigned long lastBlink = 0;
        if (millis() - lastBlink > 1000) {
            lastBlink = millis();
            digitalWrite(BLE_LED, !digitalRead(BLE_LED));
        }
    }
    
    delay(100);
}