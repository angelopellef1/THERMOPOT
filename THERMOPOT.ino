#include <Adafruit_SSD1306.h>
#include <stdio.h>
#include <WiFiClient.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
//#include <EEPROM.h>
#include <TFT_eSPI.h> 
#include "AsyncUDP.h"
#include <PubSubClient.h>

/**



Thermo pot

   2

1      3


Variamo resistenza fra 1 e 2 con manopola, R12 è zero con manopola al minimo
Mettere manopola a 24 gradi

 Il trick è mettere una resistenza in parallelo a R12 e staccarla grazie al rele


 Collegare resistenza fra 1 e D del rele

 Quando il gpio è LOW-> corto CD ->Req = R12
 Quando GPIO è HIGH -> cort DL -> Req = parallelo R12 e R1k -> 12gradi


*/


#define MAX_WIFI_INIT_RETRY 10
#define WIFI_RETRY_DELAY 1000

#define CURSOR_X0 50
#define CURSOR_Y0 50


#define RISCA_ON  LOW
#define RISCA_OFF HIGH

#define RELEPIN  22

// Use hardware SPI
TFT_eSPI tft = TFT_eSPI(200,320);
AsyncUDP udp;

struct Thermo_T
{
  float set_point_c;
  float t1;
}Thermo;


void SetCursor(int x, int y)
{
    tft.setCursor(CURSOR_X0+x, CURSOR_Y0 + y, 2);
}

unsigned long drawTime = 0;

void setup(void) {
  Serial.begin(115200);
  tft.begin();
  tft.setRotation(1);
  
  //init relay
   pinMode(RELEPIN, OUTPUT);
   digitalWrite(RELEPIN,RISCA_OFF);  //L C D   in corto C e D


   //init var
   Thermo.t1 = 18;
   Thermo.set_point_c = 17;

}


static int task = 0;

int x,y,retries = 0;


const char* ssid = "Pifi";
const char* password =  "fastwebwifiangelopelle9cw";
const char* mqttServer = "192.168.0.121";//"m11.cloudmqtt.com";
const int mqttPort = 1883;
const char* mqttUser = "mqtt_user";
const char* mqttPassword = "mqtt_user";
 
WiFiClient espClient;
PubSubClient client(espClient);
//if (strcmp(topic, "esp32/relay1") == 0) {


const char* topic_t1 = "termo/t1";
const char* topic_setpoint = "termo/set";



static bool state = false;



const byte red = 23;
const byte green = 50;
const byte blue = 113;

byte color_blue[3] = {red,green,blue};
byte color_orange[3] = {(byte)232,(byte)157,(byte)19};
byte* color = (byte *)&color_blue;


void callback(char* topic, byte* payload, unsigned int length) 
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i=0;i<length;i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  String svalue = String((char*)payload);
  svalue = svalue.substring(0,4);

  ////////////////////////EXPERIMENTAL GUI//////////
  if (strcmp(topic, topic_t1) == 0) 
  {
    SetCursor(0,30);
    Thermo.t1 = svalue.toFloat();
  }
  else
  {
     SetCursor(0,60);
     Thermo.set_point_c = svalue.toFloat();
  }
  tft.print(topic);
  tft.print(" ");
  //for (int i=0;i<length;i++) {
   // tft.print((char)payload[i]);
  //}
  
  tft.print(svalue);
  ////////////////////////////////////////////////////

  
  if(Thermo.set_point_c > Thermo.t1)
  {
    digitalWrite(RELEPIN,RISCA_ON);
    tft.fillRect(240, 50, 40, 40, TFT_ORANGE);
    
  }
  else
  {
    digitalWrite(RELEPIN,RISCA_OFF);
    tft.fillRect(240, 50, 40, 40, (color_blue[0],color_blue[1],color_blue[2]));
  }
  
 
  
  
}

void loop()
{
  switch(task)
  {
    case 0:
       WiFi.begin(ssid, password);
 
      while (WiFi.status() != WL_CONNECTED)
      {
        delay(500);
        Serial.println("Connecting to WiFi..");
      }
      Serial.println("Connected to the WiFi network");
 
      client.setServer(mqttServer, mqttPort);
      client.setCallback(callback);
 
      while (!client.connected()) 
      {
        Serial.println("Connecting to MQTT...");
        if (client.connect("ESP32Client", mqttUser, mqttPassword )) 
        {
          Serial.println("connected");  
          task = 1;
        } 
        else 
        {
          Serial.print("failed with state ");
          Serial.print(client.state());
          delay(2000);
        }
      }
 
      client.subscribe(topic_t1);
      client.subscribe(topic_setpoint);
      
          
    break;

    case 1:
        tft.fillScreen((color[0],color[1],color[2]));
        SetCursor(0,0);
        // Set the font colour to be white with a black background, set text size multiplier to 1
        tft.setTextColor(TFT_WHITE,(color[0],color[1],color[2]));  
        tft.setTextSize(2);
        // We can now plot text on screen using the "print" class
        tft.print("MQTT in ");
        tft.println( WiFi.SSID());
        tft.println( );
        task = 3;
    break;

   

    case 3:
      client.loop();
    break;
    
  }
}
