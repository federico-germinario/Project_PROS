#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include "settings.h"
#include <ThingSpeak.h>

String st;
String content;
int statusCode;
bool data_available = false;  

WiFiClient client;

//Dichiaro un server locale sulla porta 80 
ESP8266WebServer server(80);

unsigned long myChannelNumber = CH_ID;
const char * myWriteAPIKey = APIKEY;

String currentLine;

float lastLongitude;
float lastLatitude;
float lastPpm;
int lastRoadQuality;
int lastFallDetected;

void read_serial_packet();
void send_to_thingsspeak();
void connect_to_wifi(String ssid, String pass);
bool testWifi();
void launchWeb();
void setupAP();
void createWebServer();

void setup() {
  Serial.begin(115200);
  WiFi.disconnect();
  EEPROM.begin(512);                //Inizializzazione EEPROM
  delay(10);

  pinMode(LED_BUILTIN, OUTPUT);     //Inizializzazione LED_BUILTIN come pinout
  pinMode(2, OUTPUT);               //Inizializzazione GPIO2 come pinout

  digitalWrite(LED_BUILTIN, HIGH);  //Led spento
  digitalWrite(2, LOW);         
  
  Serial.setTimeout(1000);          //Aspetto max 1 sec una stringa sulla seriale
  
  String esid;
  for (int i = 0; i < 32; ++i){
    esid += char(EEPROM.read(i));
  }
 
  String epass = "";
  for (int i = 32; i < 96; ++i){
    epass += char(EEPROM.read(i));
  }

  WiFi.mode(WIFI_STA);               //Solo client
  ThingSpeak.begin(client);          //Inizializzazione ThingSpeak
  connect_to_wifi(esid.c_str(), epass.c_str());
}

void loop() {
  read_serial_packet();
  if(data_available){
    send_to_thingsspeak();
    data_available = false;
  } 
}

bool testWifi(void){
  int c = 0;
  //Aspetto di connettermi al WiFi
  while ( c < 20 ) {
    if (WiFi.status() == WL_CONNECTED){
      return true;
    }
    delay(500);
    c++;
  }
  //Connessione fallita
  return false;
}

void read_serial_packet() {
  if(Serial.available()) {
    data_available = true;
    currentLine = Serial.readStringUntil('\n');

    int commaSplitIndex = currentLine.indexOf(',');
    if (commaSplitIndex > 0) {
      String longitudeStr = currentLine.substring(0, commaSplitIndex);
      currentLine = currentLine.substring(commaSplitIndex + 1);
      commaSplitIndex = currentLine.indexOf(',');
      String latitudeStr = currentLine.substring(0, commaSplitIndex);
      currentLine = currentLine.substring(commaSplitIndex + 1);
      commaSplitIndex = currentLine.indexOf(',');
      String ppmStr = currentLine.substring(0, commaSplitIndex);
      currentLine = currentLine.substring(commaSplitIndex + 1);
      commaSplitIndex = currentLine.indexOf(',');
      String roadQualityStr = currentLine.substring(0, commaSplitIndex);
      String fallDetectedStr = currentLine.substring(commaSplitIndex + 1);

      lastLongitude = longitudeStr.toFloat();
      lastLatitude = latitudeStr.toFloat();
      lastPpm = ppmStr.toFloat();
      lastRoadQuality = roadQualityStr.toInt();
      lastFallDetected = fallDetectedStr.toInt();
    }
  }
}

void connect_to_wifi(String ssid, String pass) {  
  WiFi.begin(ssid, pass); //Connessione alla rete WiFi
  delay(1000);
  if (testWifi()){
    digitalWrite(LED_BUILTIN, LOW); //Accendi led
    
    digitalWrite(2, HIGH); //Invia un segnale alto su GPIO2 per 50 ms
    delay(50); 
    digitalWrite(2, LOW);         
  }else{
    // La connessione alla rete WiFi è fallitta
    // Accensione Hotspot WiFi
    setupAP();
    
    while ((WiFi.status() != WL_CONNECTED)){
      delay(20);
      server.handleClient();
    }

    digitalWrite(LED_BUILTIN, LOW); //Accendi led 
    digitalWrite(2, HIGH);          //Invia un segnale alto su GPIO2 per 50 ms
    delay(50); 
    digitalWrite(2, LOW);  
  }
}

void launchWeb(){
  createWebServer();
  server.begin(); // Start server
}

void setupAP(void){
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  st = "<ol>";
  for (int i = 0; i < n; ++i){
    // Stampa gli ssid per ogni WiFi trovato 
    st += "<li>";
    st += WiFi.SSID(i);
    st += " (";
    st += WiFi.RSSI(i);

    st += ")";
    st += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*";
    st += "</li>";
  }
  st += "</ol>";
  delay(100);
  WiFi.softAP("ESP8266", "");
  launchWeb();
}

void createWebServer(){
  {
    server.on("/", []() {
      content = "<!DOCTYPE HTML>\r\n<html>";
      content += "\r\nReti disponibili:\r\n";
      content += "<p>";
      content += st;
      content += "</p><form method='get' action='setting'><label>SSID: </label><input name='ssid' length=32><br><label>PASSWORD: </label><input name='pass' length=64><input type='submit'></form>";
      content += "<form action=\"/scan\" method=\"POST\"><input type=\"submit\" value=\"scan\"></form>";
      content += "</html>";
      server.send(200, "text/html", content);
    });

    server.on("/scan", []() {
      setupAP();
      IPAddress ip = WiFi.softAPIP();
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);

      content = "<!DOCTYPE HTML>\r\n<html>go back";
      server.send(200, "text/html", content);
    });

    server.on("/setting", []() {
      String qsid = server.arg("ssid");
      String qpass = server.arg("pass");
      if (qsid.length() > 0 && qpass.length() > 0) {
        // Pulizia EEPROM
        for (int i = 0; i < 96; ++i) {
          EEPROM.write(i, 0);
        }

        // Scrittura ssid su eeprom
        for (int i = 0; i < qsid.length(); ++i){
          EEPROM.write(i, qsid[i]);
        }

        // Scrittura password su eeprom
        for (int i = 0; i < qpass.length(); ++i){
          EEPROM.write(32 + i, qpass[i]);
        }

        EEPROM.commit();
        content = "{\"Success\":\"saved to eeprom... reset to boot into new wifi\"}";
        statusCode = 200;
        ESP.reset();
      }else {
        content = "{\"Error\":\"404 not found\"}";
        statusCode = 404;
        Serial.println("Sending 404");
      }

      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(statusCode, "application/json", content);
    });
  }}

void send_to_thingsspeak() {
  ThingSpeak.setField(2, lastLongitude);
  ThingSpeak.setField(3, lastLatitude);
  ThingSpeak.setField(4, lastPpm);
  ThingSpeak.setField(5, lastRoadQuality);
  ThingSpeak.setField(6, lastFallDetected);
  
  //Invio i dati a ThingSpeak tramite chiamata REST HTTPS
  ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);

  //Attivo la modalità deep sleep finchè non riceverò un hard reset
  ESP.deepSleep(0); 
}