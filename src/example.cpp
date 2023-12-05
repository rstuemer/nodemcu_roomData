/*********
    Credits:
  WiFi Manager Library und allgemeines Vorgehen: https://github.com/tzapu/WiFiManager
  Rui Santos: JSON Routine für Parameterspeicherung - JSON Library muss < v6 sein!
              project details at http://randomnerdtutorials.com
  Mit LittleFS realisiert anstatt SPIFFS

  Beim Start versucht ESP 8266zu connecten, wenn kein WLAN gefunden, dann
  Start Captive Portal zur Konfiguration von SSID, PSK, MQTT Server, Username, Topic Pfade
*********/

#include <LittleFS.h> //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          // https://github.com/bblanchon/ArduinoJson

#include "DHT_U.h"
#include <MQTT.h>

#define DHTTYPE DHT22
#define DHTPIN 14

// Set web server port number to 80
WiFiServer server(80);
WiFiClient net;
MQTTClient mqtt;

DHT dht(DHTPIN, DHTTYPE);
char mqtt_server[15] = "IP Adresse";
char mqtt_user[20] = "MQTT User";
char mqtt_password[20] = "MQTT Password";
char mqtt_pathtemp[40] = "/Pfad/Temperatur";
char mqtt_pathhum[40] = "/Pfad/Luftfeuchtigkeit";

int MaxReconnects = 5; // Max Anzahl MQTT Server Verbindungsversuche
int MQTTReconnects = 0; // Zähler für MQTT Reconnects
int WiFiConnRetry=0; // Zähler für WiFi Connect Retrys
int WiFiConnMax=100; // Max Anzahl WiFi Verbindungsversuche

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Booting...");

  //clean FS, for testing
  //LittleFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (LittleFS.begin()) {
    Serial.println("mounted file system");
    if (LittleFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = LittleFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_user, json["mqtt_user"]);
          strcpy(mqtt_password, json["mqtt_password"]);
          strcpy(mqtt_pathtemp, json["mqtt_pathtemp"]);
          strcpy(mqtt_pathhum, json["mqtt_pathhum"]);
        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 15);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 20);
  WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqtt_password, 20);
  WiFiManagerParameter custom_mqtt_pathtemp("pathtemp", "mqtt path temp", mqtt_pathtemp, 40);
  WiFiManagerParameter custom_mqtt_pathhum("pathhum", "mqtt path hum", mqtt_pathhum, 40);

  // WiFiManager
  // Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  // set custom ip for portal
  //wifiManager.setAPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_mqtt_pathtemp);
  wifiManager.addParameter(&custom_mqtt_pathhum);

  // Uncomment and run it once, if you want to erase all the stored information
  //wifiManager.resetSettings();

  //set minimum quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setTimeout(180);

  // fetches ssid and pass from eeprom and tries to connect
  // if it does not connect it starts an access point with the specified name
  // here  "AutoConnectAP"
  // and goes into a blocking loop awaiting configuration
  // wifiManager.autoConnect("RustiSensor");  // Normale Routine
  // or use this for auto generated name ESP + ChipID
  // wifiManager.autoConnect();

  // Start WiFi Manager mit Timeout Überprüfung. Nach Timeout... Tiefschlaf
  if (!wifiManager.autoConnect("RustiSensor")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.deepSleep(0); // sleep forever
  }

  // if you get here you have connected to the WiFi
  Serial.println("Connected.");

  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  strcpy(mqtt_pathtemp, custom_mqtt_pathtemp.getValue());
  strcpy(mqtt_pathhum, custom_mqtt_pathhum.getValue());

  Serial.println("saved MQTT params:");
  Serial.println(mqtt_server);
  Serial.println(mqtt_user);
  Serial.println(mqtt_password);
  Serial.println(mqtt_pathtemp);
  Serial.println(mqtt_pathhum);

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_password"] = mqtt_password;
    json["mqtt_pathtemp"] = mqtt_pathtemp;
    json["mqtt_pathhum"] = mqtt_pathhum;

    LittleFS.format(); // weiss nicht, ob das nötig ist, schadet aber nicht.
    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  WiFi.softAPdisconnect (true); // turns off Access Point visibility in net neighbourhood

  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  //++++++++++++++++++++++++Sensor activity & MQTT publishing++++++++++++++++++++
  //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

 //WiFi.begin(ssid, password); // not needed! WiFi Manager's job!
 // Nachfolgende WHILE Schleife wird benötigt, um sauber mit dem Router zu connecten. 
 // Insbesondere die Fritz!Box hat sonst manchmal Probleme beim Aufwachen aus dem Stromsparmodus. 
   while (WiFi.status() != WL_CONNECTED) {
      delay(100); 
      Serial.print(".");
      WiFiConnRetry++;
      if (WiFiConnRetry >= WiFiConnMax){
         Serial.println("No WiFi Connect, sleep 30 mins");
         delay(1000);
         ESP.deepSleep(1800e6);
       }
   }

  mqtt.begin(mqtt_server, net);
  mqtt.loop();
  delay(10);
  while (!mqtt.connect(mqtt_server, mqtt_user, mqtt_password)) {
    Serial.print("MQTT connect ");
    Serial.println(MQTTReconnects);
    MQTTReconnects++;
    if (MQTTReconnects > MaxReconnects) {
      Serial.println("No MQTT Connect, going to sleep");
      delay(1000);
      ESP.deepSleep(1800e6);  //30 Minuten
    }
    delay(500);
  }
  Serial.println("MQTT connection established!");

  dht.begin();
  delay(2000);

  float temp = dht.readTemperature();
  float humidity = dht.readHumidity();
  Serial.print("MQTT Status: ");
  Serial.println(mqtt.connected());
  Serial.print("Sending... ");
  Serial.println();
  if (!isnan(humidity) || !isnan(temp)) {
    Serial.print("Temp: ");
    Serial.print(String(temp));
    Serial.print(" Humidity: ");
    Serial.println(String(humidity));
    mqtt.publish(String(mqtt_pathtemp), String(temp));
    mqtt.publish(String(mqtt_pathhum), String(humidity));
    mqtt.disconnect();
  }

  Serial.println("Going into deep sleep for xx seconds");
  ESP.deepSleep(1800e6); // e.g. 20e6 is 20 seconds
}

void loop() {}