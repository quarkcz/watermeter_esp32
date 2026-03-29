/**
 * ESP32 Water Meter
 * @author Karel Quast <karel@quast.cz>
 * @copyright 2026
 * @brief Water meter based on ESP32 for impulse sensor with open-draing architecture
 *
 * Attribution: ESP32 Wifi Manager, Martin Verges <martin@verges.cc>
 *
 * Licensed under CC BY-NC-SA 4.0
**/

#include <Arduino.h>
#include <vodomer.h>
#include <wifimanager.h>
#include <LittleFS.h>
#include <Wire.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/Picopixel.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <esp_wifi.h>  // ESP-IDF API
#include <time.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <SHA3.h>
#include <ikony.h>

#define SDA_PIN 8
#define SCL_PIN 9
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define PIN_RESET_BUTTON 0
#define PIN_CITAC 1
#define DEBOUNCE_MS 50
#define LONG_PRESS_MS 8000

const char* cVodomerState = VODOMER_STATE_INIT;
const unsigned int cDisplayOnTimeSeconds = 120;
uint32_t cDisplayOnTimeStart = 0;
uint32_t cLastTimeSyncMillis = 0;
const unsigned int cSyncTimeIntervalSeconds = 12 * 60 * 60; //12h
bool cDisplayIsOn = false;
String cVodomerID = "????";
String ap_password = "waterm123";
const String ap_ssid_prefix = "waterm_";
String ap_ssid;
bool vBtnShortPressed = false;
SHA3_256 sha3;

static bool timeSynced = false;
static const char* NTP1 = "pool.ntp.org";
static const char* NTP2 = "time.google.com";
const char* TZ_GMT = "GMT0";

int cButtonLongPressRemainingTimeSeconds = 0;  // zbývající sekundy do LONG_PRESS_MS
volatile bool cIsReseting = false;             // true po začátku držení pro dlouhý stisk
volatile bool btnPressed = false;              // true while button is held
volatile uint32_t pressStartMs = 0;            // time when button went LOW
volatile uint32_t lastEdgeMs = 0;              // for debounce - reset btn
volatile uint32_t lastVodomerEdgeMs = 0;       // for debounce - vodomer counter
volatile bool longFired = false;               // to fire long press only once
volatile long vVodomerCounter = 0;             // hlavní počítadlo pulzů
long vVodomerCounterLastSaved = 0;             // jaká hodnota byla naposledy uložena do paměti
volatile bool vVodomerImpulsePressed = false;  // jestli je vstup z čisla aktivní nebo neaktivní

long vDBVodomerCounterLastSent = 0;            // jaká hodnota byla naposledy poslána do databáze
int cDBSaveIntervalSecondsMin = 10;            // jak často se má volat script pro uložení do vzdálené databáze - nejkratší úsek
int cDBSaveIntervalSecondsMax = 30*60;         // jak často se má volat script pro uložení do vzdálené databáze - nejdelší úsek po kterém se vždy zapíše i beze změny počítadla (keep alive)
uint32_t cDBSaveLastCallMillis = 0;            // kdy bylo naposledy úspěšně uloženo do databáze

String cDBAPIURLBaseDefault = "https://your-server.com/sensor/sensor-data.php";
String cDBAPIURLBase = "";
String cDBAPISensorValueName = "";
String cDBAPISensorValueNameDefault = "sensor_value";
String cDBAPISenrodAdditional = "";
String cDBAPISenrodAdditionalDefault = "sensor_additional";
String cDBAPISensorGMT = "";
String cDBAPISensorGMTDefault = "dt_gmt";
String cDBAPISensorSendKeyAttrName = ""; 
String cDBAPISensorSendKeyAttrNameDefault = "sensor_key"; 
String cDBAPISensorSendKey = ""; //no default
String cDBAPISensorSensorIDAttrName = ""; 
String cDBAPISensorSensorIDAttrNameDefault = "sensor_id";
String vDBAPIUrlToSend = "";

WIFIMANAGER WifiManager;
AsyncWebServer webServer(80);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Preferences prefs;

bool isNumber(const String &str) {
    if (str.length() == 0) return false;
    const char *cstr = str.c_str();
    char *endptr;
    strtol(cstr, &endptr, 10);  // pokus o převod na číslo (základ 10)
    return (*endptr == '\0');   // pokud endptr ukazuje na konec, je to čisté číslo
}

void syncTimeFromNtpIfConnected() {
    if (WiFi.status() != WL_CONNECTED) return;
    Serial.print("[TIME] Synchronizing time...");

    timeSynced = false;

    // Nastaví časové pásmo a spustí SNTP klienta
    configTzTime(TZ_GMT, NTP1, NTP2);

    // Počkej na první synchronizaci (max ~15 s)
    struct tm tmInfo;
    const uint8_t retries = 10;
    for (uint8_t i = 0; i < retries; i++) {
        if (getLocalTime(&tmInfo, 1500)) { // timeout 1.5 s na pokus
            timeSynced = true;
            break;
        }
    }

    if (timeSynced) {
        Serial.printf("... Synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                    tmInfo.tm_year + 1900, tmInfo.tm_mon + 1, tmInfo.tm_mday,
                    tmInfo.tm_hour, tmInfo.tm_min, tmInfo.tm_sec);
        cLastTimeSyncMillis = millis();
    } else {
        Serial.println("... NTP sync failed");
    }
}

String getDateTimeString() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "1970-01-01 00:00:00"; // fallback, když není čas dostupný
    }

    char buf[20]; // "yyyy-mm-dd hh:MM:ss" = 19 znaků + \0
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(buf);
}

void onShortPress() {
    vBtnShortPressed = true;
}

void onLongPress() {
    //Smazat nastavení Wifi manažeru
    Preferences p;
    p.begin("wifimanager", false);
    p.clear();        // smaže všechny klíče v tomto namespace
    p.end();
    Serial.println("[BTN] LONG press -> RESET WIFI!");
    ESP.restart();
}

IRAM_ATTR void handleEdge() {
    uint32_t now = millis();
    if (now - lastEdgeMs < DEBOUNCE_MS) return;
    lastEdgeMs = now;

    bool levelLow = (digitalRead(PIN_RESET_BUTTON) == LOW);
    if (levelLow) {
        // FALLING = pressed
        btnPressed   = true;
        pressStartMs = now;
        longFired    = false;
        cIsReseting  = true;                  
    } else {
        // RISING = released
        bool wasLong = longFired;
        btnPressed = false;
        cIsReseting  = false;                     
        cButtonLongPressRemainingTimeSeconds = 0;   
        if (!wasLong) {
            uint32_t held = now - pressStartMs;
            if (held >= DEBOUNCE_MS) onShortPress();
        }
    }
}

IRAM_ATTR void handleVodomerEdge() {
    uint32_t now = millis();

    vVodomerImpulsePressed = (digitalRead(PIN_CITAC) == LOW);

    if (now - lastVodomerEdgeMs < DEBOUNCE_MS) {
        return;
    }

    if (!vVodomerImpulsePressed) {
        vVodomerCounter++;
    }

    lastVodomerEdgeMs = now;
}

String getSoftAPPassword() {
    wifi_config_t conf{};
    if (esp_wifi_get_config(WIFI_IF_AP, &conf) == ESP_OK) {
        // prázdný string = otevřené AP (bez hesla)
        return String(reinterpret_cast<const char *>(conf.ap.password));
    }
    return String();
}

String sha3256ToHex(const char* input) {
    uint8_t hash[SHA3_256::HASH_SIZE];
    sha3.reset();
    sha3.update((const uint8_t *)input, strlen(input));
    sha3.finalize(hash, sizeof(hash));

    char buf[SHA3_256::HASH_SIZE * 2 + 1];
    for (size_t i = 0; i < sizeof(hash); i++) {
        sprintf(buf + (i * 2), "%02x", hash[i]); // převod byte -> hex
    }
    buf[sizeof(hash) * 2] = '\0';
    return String(buf);
}

// Povolené znaky v URL ponecháme, ostatní %-enkódujeme.
// Zachováváme : / ? & = % aby se nerozbila struktura URL.
static String urlEncodeUrl(const String &in) {
    String out;
    out.reserve(in.length() * 3); // worst-case
    const char* hex = "0123456789ABCDEF";

    auto isAllowed = [](char c) -> bool {
        // alnum + bezpečné + URL strukturální znaky
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) return true;
        switch (c) {
            case '-': case '_': case '.': case '~':
            case ':': case '/': case '?': case '&':
            case '=': case '%': // už může být enkódováno
            return true;
        }
        return false;
    };

    for (size_t i = 0; i < in.length(); ++i) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (isAllowed((char)c)) {
            out += (char)c;
        } else {
            out += '%';
            out += hex[(c >> 4) & 0x0F];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

/**
 * Zavolá GET na danou URL (HTTP/HTTPS) s timeoutem 5 s.
 * Vrací true, pokud HTTP 200 a payload je přesně "OK"; jinak false.
 */
bool dbSendData(String url) {
    if (url.length() == 0) return false;
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    int httpCode = 0;
    String payload;

    http.setTimeout(5000); // 5 s
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    // Necháme HTTPClient, ať si podle schématu vybere správný (secure/plain) klient.
    // Tohle je nejspolehlivější i při redirectech mezi http<->https.
    bool begun = http.begin(url);
    if (!begun) {
        Serial.println("[HTTP][ERROR] nepodařilo se inicializovat HTTP klienta pro URL: " + url);
        return false;
    }

    httpCode = http.GET();
    if (httpCode > 0) {
        payload = http.getString();
    } else {
        http.end();
        Serial.println("[HTTP][ERROR] chyba přenosu / timeout (httpCode < 0)");
        return false;
    }

    http.end();

    payload.trim();

    if (httpCode == HTTP_CODE_OK && payload == "OK") {
        return true;
    }
    Serial.printf("[HTTP] code=%d, payload='%s'\n", httpCode, payload.c_str());
    Serial.println("[HTTP][ERROR] Server response: " + payload);
    return false;
}

void drawTextCentered(Adafruit_GFX &disp, String txt, int x1, int x2, int y) {
    int16_t bx, by;
    uint16_t w, h;

    disp.getTextBounds(txt, 0, 0, &bx, &by, &w, &h);

    int areaWidth = x2 - x1;
    int x = x1 + (areaWidth - w) / 2;

    disp.setCursor(x, y);
    disp.print(txt);
}

void displayCurrentState() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);

    if (cIsReseting) {
        display.clearDisplay();
        DrawIcon(display, "danger24x24", 1, 4);  // vykreslí vykřičník vlevo
        display.setCursor(35, 8);
        display.print("FULL RESET in:");
        display.setCursor(35, 20);
        display.print(String(cButtonLongPressRemainingTimeSeconds));
        display.print(" s");
        display.display();
    } else {
        if (WiFi.status() == WL_CONNECTED) {
            // Standardni obrazovka
            display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
            DrawIcon(display, "wifi8x7", 3, 2); 
            display.setCursor(12, 2);
            drawTextCentered(display, WiFi.localIP().toString(), 12, SCREEN_WIDTH - 2, 2);

            display.drawLine(0, 11, SCREEN_WIDTH, 11, SSD1306_WHITE);
            DrawIcon(display, "water_drop16x16", 3, 13);  

            //Pocet pulzu
            if (vVodomerCounter <= 99999999) {
                display.setCursor(19, 21);
                display.setFont(&FreeSans9pt7b);
                display.print(vVodomerCounter);
            } else {
                display.setCursor(20, 18);
                display.setFont(nullptr);
                display.setTextSize(1);
                display.print(vVodomerCounter);
            }

            display.setTextSize(1);
            display.setFont(&Picopixel);
            display.drawLine(SCREEN_WIDTH - 26, 11, SCREEN_WIDTH - 26, SCREEN_HEIGHT - 7, SSD1306_WHITE);
            if (vVodomerImpulsePressed) {
                drawTextCentered(display, "ON", SCREEN_WIDTH - 16, SCREEN_WIDTH, 20); 
                display.fillCircle(SCREEN_WIDTH - 20, 18, 3, SSD1306_WHITE);
            } else {
                drawTextCentered(display, "OFF", SCREEN_WIDTH - 16, SCREEN_WIDTH, 20);
                display.drawCircle(SCREEN_WIDTH - 20, 18, 3, SSD1306_WHITE);
            }
            display.setFont(nullptr);
        } else if (WiFi.getMode() & WIFI_AP) {
            // Přístupový bod
            DrawIcon(display, "wifi_cover24x24", SCREEN_WIDTH - 25, 0); 

            DrawIcon(display, "four_dots_narrow16x5", 1, 3); 
            display.setCursor(20, 1);
            display.println(WiFi.softAPIP());

            DrawIcon(display, "wifi8x7", 4, 12); 
            display.setCursor(20, 12);
            display.println(WiFi.softAPSSID());

            DrawIcon(display, "lock_2", 4, 23); 
            display.setCursor(20, 23);
            display.println(getSoftAPPassword());
        } else {
            // Není ani WiFi ani AP
            DrawIcon(display, "wifi_no_signal24x24", 1, 4);  // vykreslí vykřičník vlevo
            display.setCursor(28, 16);
            display.setFont(&FreeSans9pt7b);
            display.println("No WiFi");
            display.setFont(nullptr);
        }

        int remSec = cDisplayOnTimeSeconds - ((millis() - cDisplayOnTimeStart) / 1000);
        display.fillRect(SCREEN_WIDTH - 26, SCREEN_HEIGHT - 7, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
        display.setFont(&Picopixel);
        display.setTextColor(SSD1306_BLACK);
        drawTextCentered(display, String(remSec) + "s", SCREEN_WIDTH - 24, SCREEN_WIDTH, SCREEN_HEIGHT - 2);
        display.setFont(nullptr);
        display.setTextColor(SSD1306_WHITE);
    }

    display.display();
}

void readPrefferences() {
    prefs.begin("vodomer", false); 

    cVodomerID = prefs.getString("cVodomerID", "9999");
    vVodomerCounter = prefs.getLong("vVodomerCounter", 0);

    cDBAPIURLBase = prefs.getString("APIURLBase", cDBAPIURLBaseDefault);
    cDBAPISensorValueName = prefs.getString("APIValueName", cDBAPISensorValueNameDefault);
    cDBAPISenrodAdditional = prefs.getString("APIAdditional", cDBAPISenrodAdditionalDefault);
    cDBAPISensorGMT = prefs.getString("cAPIGMT", cDBAPISensorGMTDefault);
    cDBAPISensorSendKey = prefs.getString("APISendKey", "");
    cDBAPISensorSendKeyAttrName = prefs.getString("APISendKeyAttrName", cDBAPISensorSendKeyAttrNameDefault);
    cDBAPISensorSensorIDAttrName = prefs.getString("APIIDAttrName", cDBAPISensorSensorIDAttrNameDefault);

    prefs.end();
}

void setWebServerEndPoints() {
    webServer.on("/", HTTP_GET, [](AsyncWebServerRequest* req) { req->send(LittleFS, "/index.html", "text/html"); });
    webServer.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        String json = "{";
        json += "\"vodomer_id\":\"" + cVodomerID + "\",";
        json += "\"counter\":" + String(vVodomerCounter) + ",";
        json += "\"cDBAPIURLBase\":\"" + cDBAPIURLBase + "\",";
        json += "\"cDBAPISensorValueName\":\"" + cDBAPISensorValueName + "\",";
        json += "\"cDBAPISenrodAdditional\":\"" + cDBAPISenrodAdditional + "\",";
        json += "\"cDBAPISensorGMT\":\"" + cDBAPISensorGMT + "\",";
        json += "\"cDBAPISensorSendKey\":\"" + cDBAPISensorSendKey + "\",";
        json += "\"cDBAPISensorSendKeyAttrName\":\"" + cDBAPISensorSendKeyAttrName + "\",";
        json += "\"cDBAPISensorSensorIDAttrName\":\"" + cDBAPISensorSensorIDAttrName + "\"";
        json += "}";
        req->send(200, "application/json", json);
    });
    webServer.on("/api/setcounter", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("value")) {
            req->send(400, "text/plain", "Missing value parameter");
            return;
        }
        String val = req->getParam("value")->value();
        long newValue = val.toInt();
        vVodomerCounter = newValue;

        String msg = "Počítadlo nastaveno na " + String(vVodomerCounter);
        Serial.println("[API] " + msg);
        req->send(200, "text/plain", msg);
    });

    webServer.on("/api/setparam", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!req->hasParam("name") || !req->hasParam("value")) {
            req->send(400, "text/plain", "Missing name or value parameter");
            return;
        }

        String name  = req->getParam("name")->value();
        String value = req->getParam("value")->value();
        String msg;

        prefs.begin("vodomer", false);

        if (name == "counter") {
            if (!isNumber(value)) {
                prefs.end();
                req->send(400, "text/plain", "Invalid number format");
                return;
            }

            vVodomerCounter = value.toInt();
            prefs.putLong("vVodomerCounter", vVodomerCounter);
            msg = "Počítadlo nastaveno na " + String(vVodomerCounter);
            Serial.println("[API] " + msg);
        }        
        else if (name == "cDBAPIURLBase") {
            cDBAPIURLBase = value;
            prefs.putString("APIURLBase", cDBAPIURLBase);
        }
        else if (name == "cDBAPISensorValueName") {
            cDBAPISensorValueName = value;
            prefs.putString("APIValueName", cDBAPISensorValueName);
        }
        else if (name == "cDBAPISenrodAdditional") {
            cDBAPISenrodAdditional = value;
            prefs.putString("APIAdditional", cDBAPISenrodAdditional);
        }
        else if (name == "cDBAPISensorGMT") {
            cDBAPISensorGMT = value;
            prefs.putString("APIGMT", cDBAPISensorGMT);
        }
        else if (name == "cDBAPISensorSendKey") {
            cDBAPISensorSendKey = value;
            prefs.putString("APISendKey", cDBAPISensorSendKey);
        }
        else if (name == "cDBAPISensorSendKeyAttrName") {
            cDBAPISensorSendKeyAttrName = value;
            prefs.putString("APISendKeyAttrName", cDBAPISensorSendKeyAttrName);
        }
        else if (name == "cDBAPISensorSensorIDAttrName") {
            cDBAPISensorSensorIDAttrName = value;
            prefs.putString("APIIDAttrName", cDBAPISensorSensorIDAttrName);
        }
        else {
            prefs.end();
            req->send(400, "text/plain", "Unknown parameter name");
            return;
        }

        prefs.end();

        if (msg == "") msg = "Parametr '" + name + "' nastaven na '" + value + "'";
        req->send(200, "text/plain", msg);
        readPrefferences();
    });

    webServer.on("/api/syssetup", HTTP_GET, [](AsyncWebServerRequest* req) {
        // Ověření parametrů
        if (!req->hasParam("key") || !req->hasParam("vodomer_id")) {
            req->send(500, "text/plain", "Missing parameters");
            return;
        }

        String key = req->getParam("key")->value();
        String newVodomerID = req->getParam("vodomer_id")->value();

        // Vypočítáme očekávaný hash
        String expectedKey = sha3256ToHex("your-secret-key"); //Calculated for this text: 375a957226152167e59d81f4e9fb8a559ad535ee78cf022aa2636a52d96a20ec

        // Porovnání klíče
        if (key != expectedKey) {
            req->send(500, "text/plain", "Invalid key");
            return;
        }

        // Uložení nového ID do globální proměnné a do paměti
        cVodomerID = newVodomerID;
        prefs.begin("vodomer", false);
        prefs.putString("cVodomerID", cVodomerID);
        prefs.end();

        Serial.println("[API] cVodomerID updated to: " + cVodomerID);
        req->send(200, "text/plain", "vodomer_id updated successfully");
    });
    
    webServer.serveStatic("/assets", LittleFS, "/assets");    
    webServer.begin();
    Serial.println("HTTP server started on port 80");
} 

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== ESP32 C3 Vodomer ===");

    pinMode(PIN_RESET_BUTTON, INPUT_PULLUP); 
    attachInterrupt(digitalPinToInterrupt(PIN_RESET_BUTTON), handleEdge, CHANGE);
    pinMode(PIN_CITAC, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_CITAC), handleVodomerEdge, CHANGE);
    //Aby to spravne fungovalo, je potreba nasledujici HW zapojeni:
    // -- PIN_CITAC ma rezistor 10k vuci 3.3V
    // -- PIN_CITAC ma kondenzator 100nF vuci GND
    // -- PIN_CITAC ma rezistor 1k v serii s cidlem vodomeru (tzn. z PINu vede rezistor a na jeho druhem konci teprve cidlo)
   
    Serial.println("Initializing LittleFS");
    LittleFS.begin(false); 

    Serial.println("Initializing OLED Display");
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Initializing...");
    display.display();
    cDisplayIsOn = true;

    Serial.println("Reading preferences");
    readPrefferences();
    ap_ssid = ap_ssid_prefix + cVodomerID;

    Serial.println("Starting Wifi Manager");
    //WifiManager.setTxPower(WIFI_POWER_11dBm);
    WifiManager.configueSoftAp(ap_ssid, ap_password);
    WifiManager.startBackgroundTask();        // Run the background task to take care of our Wifi    
    WifiManager.fallbackToSoftAp(true);       // Run a SoftAP if no known AP can be reached
    WifiManager.attachWebServer(&webServer);  // Attach our API to the HTTP Webserver 
    WifiManager.attachUI();                   // Attach the UI to the Webserver

    Serial.println("Running webserver");
    setWebServerEndPoints();

    cDisplayOnTimeStart = millis();
}

void loop() {
    delay(300);

    if (vBtnShortPressed) {
        vBtnShortPressed = false;
        Serial.println("[BTN] short press -> turning display ON");
        cDisplayOnTimeStart = millis();
        if (!cDisplayIsOn) {
            cDisplayIsOn = true;
            display.ssd1306_command(SSD1306_DISPLAYON);
        }
    }

    if (cDisplayIsOn == true) {
        displayCurrentState();
    }

    if (btnPressed && !longFired) {
        uint32_t now = millis();
        if (now - pressStartMs >= LONG_PRESS_MS) {
            longFired = true;
            onLongPress();
        }
    }
    if (btnPressed && !longFired) {
        uint32_t now = millis();
        // aktualizace zbývajících sekund do dlouhého stisku
        int32_t remainingMs = (int32_t)LONG_PRESS_MS - (int32_t)(now - pressStartMs);
        if (remainingMs < 0) remainingMs = 0;
        // zaokrouhlení nahoru na celé sekundy (ceil)
        cButtonLongPressRemainingTimeSeconds = (remainingMs + 999) / 1000;

        if (now - pressStartMs >= LONG_PRESS_MS) {
            longFired = true;
            cIsReseting = false; // kosmeticky; stejně se hned restartuje
            onLongPress();       // ESP.restart()
        }
    }    

    //Je treba ulozit do vnitrni pameti posledni stav pocitadla?
    if ((vVodomerCounterLastSaved != vVodomerCounter) && (vVodomerCounter != 0)) {
        prefs.begin("vodomer", false); 
        prefs.putLong("vVodomerCounter", vVodomerCounter);
        vVodomerCounterLastSaved = vVodomerCounter;
        prefs.end();       
    }

    //Neni treba vypnout display?
    if ((cDisplayIsOn == true) && (millis() - cDisplayOnTimeStart > cDisplayOnTimeSeconds * 1000)) {
        cDisplayIsOn = false;
        Serial.println("Turning display off...");
        display.ssd1306_command(SSD1306_DISPLAYOFF);
    }

    //Neni treba synchronizovat cas?
    if ((millis() - cLastTimeSyncMillis > cSyncTimeIntervalSeconds * 1000) || (cLastTimeSyncMillis == 0)) {
        syncTimeFromNtpIfConnected();
    }

    //Neni treba zapsat do DB?
    if (
        (((vDBVodomerCounterLastSent != vVodomerCounter) && (millis() - cDBSaveLastCallMillis > cDBSaveIntervalSecondsMin * 1000)) || (cDBSaveLastCallMillis == 0)) 
        ||
        (millis() - cDBSaveLastCallMillis > cDBSaveIntervalSecondsMax * 1000)
       )
    {
        String vPulse = urlEncodeUrl((vVodomerImpulsePressed == true) ? "Pulse=ON" : "Pulse=OFF");
        vDBAPIUrlToSend = cDBAPIURLBase + "?" 
            + cDBAPISensorSendKeyAttrName + "=" + cDBAPISensorSendKey
            + "&" + cDBAPISensorSensorIDAttrName + "=" + cVodomerID
            + "&" + cDBAPISensorValueName + "=" + String(vVodomerCounter) 
            + "&" + cDBAPISensorGMT + "=" + urlEncodeUrl(getDateTimeString()) 
            + "&" + cDBAPISenrodAdditional + "=" + vPulse;

        Serial.print("Volám API... ");
        if (dbSendData(vDBAPIUrlToSend)) {
            vDBVodomerCounterLastSent = vVodomerCounter;
            Serial.println("...done (OK)");
        } else {
            Serial.println("...error!");
            //Serial.println(vDBAPIUrlToSend); //DEBUG ONLY
        }
        cDBSaveLastCallMillis = millis();
    }
   
}