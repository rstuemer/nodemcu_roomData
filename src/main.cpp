#include <Arduino.h>
#include <LittleFS.h> //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>
#include <ArduinoJson.h>

//for LED status
#include <Ticker.h>
#include "DHTesp.h" // Click here to get the library: http://librarymanager/All#DHTesp

// select which pin will trigger the configuration portal when set to LOW
#define TRIGGER_PIN 0
DHTesp dht;
Ticker ticker;
WiFiManager wm;
WiFiClient espClient;
PubSubClient mqttclient(espClient);

bool portalRunning = false;
char sensorname[19]="Enviropment_Garage";
int timeout = 120; // seconds to run ondemand config portal 
int DHT_PIN = 8;
DHTesp::DHT_MODEL_t DHT_T = DHTesp::DHT11;

unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE	(50)
char msg[MSG_BUFFER_SIZE];
int value = 0;

char mqtt_server[26] = "broker.mqtt-dashboard.com";
char mqtt_user[20] = "MQTT User";
char mqtt_password[20] = "MQTT Password";
char mqtt_pathtemp[40] = "/Pfad/Temperatur";
char mqtt_pathhum[40] = "/Pfad/Luftfeuchtigkeit";

int MaxReconnects = 5; // Max Anzahl MQTT Server Verbindungsversuche
int MQTTReconnects = 0; // Zähler für MQTT Reconnects
int WiFiConnRetry=0; // Zähler für WiFi Connect Retrys
int WiFiConnMax=100; // Max Anzahl WiFi Verbindungsversuche
//flag for saving data
bool saveMqttConfig = false;

  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 15);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 20);
  WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqtt_password, 20);
  WiFiManagerParameter custom_mqtt_pathtemp("pathtemp", "mqtt path temp", mqtt_pathtemp, 40);
  WiFiManagerParameter custom_mqtt_pathhum("pathhum", "mqtt path hum", mqtt_pathhum, 40);

const int NAMELENMAX = 40;

typedef struct keyAndValue_ {
   int key;
   char value[NAMELENMAX+1];
} keyAndValue_t;




//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  saveMqttConfig = true;
}

void tick()
{
  //toggle state
  int state = digitalRead(BUILTIN_LED);  // get the current state of GPIO1 pin
  digitalWrite(BUILTIN_LED, !state);     // set pin to the opposite state
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);

  //TODO Add Configmode to Display and 
}

void checkButton(){


  // is configuration portal requested?
  if(digitalRead(TRIGGER_PIN) == LOW) {
    delay(50);
    if(digitalRead(TRIGGER_PIN) == LOW) {
         
        // set configportal timeout
        wm.setConfigPortalTimeout(timeout);
        Serial.println("Button Pressed, Starting Portal");
        if (!wm.startConfigPortal("OnDemandAP")) {
          wm.startConfigPortal("OnDemandAP");
          Serial.println("failed to connect and hit timeout");
          delay(3000);
          //reset and try again, or maybe put it to deep sleep
          ESP.restart();
          delay(5000);
        }
       
      
    }
  }
}



// Callback function
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // In order to republish this payload, a copy must be made
  // as the orignal payload buffer will be overwritten whilst
  // constructing the PUBLISH packet.

  // Allocate the correct amount of memory for the payload copy
  byte* p = (byte*)malloc(length);
  // Copy the payload to the new buffer
  memcpy(p,payload,length);
  mqttclient.publish(topic, p, length);
  // Free the memory
  free(p);
}



void configureWifiManager(){



  
  //set config save notify callback
  wm.setSaveConfigCallback(saveConfigCallback);


    //add all your parameters here
  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_password);
  wm.addParameter(&custom_mqtt_pathtemp);
  wm.addParameter(&custom_mqtt_pathhum);

    //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wm.setTimeout(180);
}

void setupLittleFS(){
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
        DynamicJsonDocument  jsondoc(size);
        DeserializationError error = deserializeJson(jsondoc, buf.get());
        if (!error) {
          Serial.println("\nparsed json");
          strcpy(mqtt_server, jsondoc["mqtt_server"]);
          strcpy(mqtt_user, jsondoc["mqtt_user"]);
          strcpy(mqtt_password, jsondoc["mqtt_password"]);
          strcpy(mqtt_pathtemp, jsondoc["mqtt_pathtemp"]);
          strcpy(mqtt_pathhum, jsondoc["mqtt_pathhum"]);                   
        }else{
           Serial.println("failed to load json config");
        }
          
       
        
          
        
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
}

void showWifiSymbolInDisplay(){

}

void connectWifi() {
  // reset settings - for testing
  // wifiManager.resetSettings();

  // set callback that gets called when connecting to previous WiFi fails, and
  // enters Access Point mode
  wm.setAPCallback(configModeCallback);

  // fetches ssid and pass and tries to connect
  // if it does not connect it starts an access point with the specified name
  // here  "AutoConnectAP"
  // and goes into a blocking loop awaiting configuration
  if (!wm.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    // reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }

  // if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");
}

void readMqttConfigFromWifi(){
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
}

void saveConfigToLittleFS(){
//save the custom parameters to FS
  if (saveMqttConfig) {
    Serial.println("saving config");
    DynamicJsonDocument doc(1024);
    doc["mqtt_server"] = mqtt_server;
    doc["mqtt_user"] = mqtt_user;
    doc["mqtt_password"] = mqtt_password;
    doc["mqtt_pathtemp"] = mqtt_pathtemp;
    doc["mqtt_pathhum"] = mqtt_pathhum;
   

    LittleFS.format(); // weiss nicht, ob das nötig ist, schadet aber nicht.
    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

     serializeJson(doc, Serial);
     
     serializeJson(doc, configFile);
    configFile.close();
    //end save
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!mqttclient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (mqttclient.connect(clientId.c_str(),mqtt_user,mqtt_password)) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      mqttclient.publish("outTopic", "hello world");
      // ... and resubscribe
      mqttclient.subscribe("inTopic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttclient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void publishMqttFloatMessage(float msg, const char* mqtt_pathtemp) {
 
  Serial.print("Publish message: ");
  Serial.println(msg);
  mqttclient.publish(mqtt_pathtemp, String(msg).c_str());
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  
  //set led pin as output
  pinMode(BUILTIN_LED, OUTPUT);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);

  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);
  configureWifiManager();
  ticker.detach();
  setupLittleFS();

  connectWifi();
  readMqttConfigFromWifi();
  showWifiSymbolInDisplay();

  if(saveMqttConfig){
    saveConfigToLittleFS();
  }
  //keep LED on
  digitalWrite(BUILTIN_LED, LOW);
  dht.setup(DHT_PIN, DHT_T); // Connect DHT sensor to GPIO 17
  mqttclient.setServer(mqtt_server, 1883);
  
  mqttclient.setCallback(mqttCallback);


}



void loop() {
  checkButton();
  if (!mqttclient.connected()) {
    reconnect();
  }
  mqttclient.loop();
   unsigned long now = millis();
  if (now - lastMsg > 2000) {
    lastMsg = now;
    ++value;

    TempAndHumidity msg = dht.getTempAndHumidity();
    publishMqttFloatMessage(msg.temperature,mqtt_pathtemp);

    publishMqttFloatMessage(msg.humidity,mqtt_pathhum);
  }
}


