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




 Quando il gpio è LOW-> corto CD ->Req = R12
 Quando GPIO è HIGH -> cort DL -> Req = parallelo R12 e R1k -> 12gradi


*/


#define MAX_WIFI_INIT_RETRY 10
#define WIFI_RETRY_DELAY 1000

#define CURSOR_X0 50
#define CURSOR_Y0 50

#define SCREEN_HEIGHT 135
#define SCREEN_WIDTH  240


#define RISCA_ON  LOW
#define RISCA_OFF HIGH

#define RELEPIN  22
#define TFT_WALL_BLUE 0x1947

#define CS_WIFI   0x01
#define CS_MQTT   0x02
#define CS_NEWDATA  0x04



// Use hardware SPI
TFT_eSPI tft = TFT_eSPI(SCREEN_HEIGHT,SCREEN_WIDTH);
AsyncUDP udp;

struct Thermo_T
{
  float set_point_c;
  float t1;
  float t2;
  float tExt;
  int presence;
  int connection_state;
}Thermo;


void SetCursor(int x, int y)
{
    tft.setCursor(0+x, 0 + y, 2);
}

void DrawRect(unsigned short  x, unsigned short  y, unsigned short  w, unsigned short l, unsigned short colo)
{
     tft.fillRect(x + 0, y + 0, w, l, colo);
}

unsigned long drawTime = 0;

void setup(void) 
{
  Serial.begin(115200);

  pinMode(TFT_BL, OUTPUT);
  ledcSetup(0, 5000, 8); // 0-15, 5000, 8
  ledcAttachPin(TFT_BL, 0); // TFT_BL, 0 - 15
  ledcWrite(0, 100); // 0-15, 0-255 (with 8 bit resolution); 0=totally dark;255=totally shiny
  
  tft.begin();
  tft.setRotation(1);
  
  //init relay
   pinMode(RELEPIN, OUTPUT);
   digitalWrite(RELEPIN,RISCA_OFF);  //L C D   in corto C e D


   //init var
   Thermo.t1 = 18;
   Thermo.t2 = 18;
   Thermo.tExt = 18;
   Thermo.presence = 2; //presence unknown
   Thermo.set_point_c = 17;

}


static int task = 0;

int x,y,retries = 0;


const char* ssid = "Pifi";
const char* password =  "fastwebwifiangelopelle9cw";
const char* mqttServer = "192.168.0.112";//"m11.cloudmqtt.com";
const int mqttPort = 1883;
const char* mqttUser = "mqtt_user";
const char* mqttPassword = "mqtt_user";
 
WiFiClient espClient;
PubSubClient client(espClient);
//if (strcmp(topic, "esp32/relay1") == 0) {

#define MAX_TOPICS  5
const char* topic_t1 = "termo/t1"; //salotto
const char* topic_t2 = "termo/t2"; //soffitta
const char* topic_tExt = "termo/tExt"; //esterno
const char* topic_setpoint = "termo/set";
const char* topic_presence= "termo/presence"; //topic di presenza 1=salotto 2=soffitta 3=tutto 4=nessuna


const char* topics[MAX_TOPICS] = {topic_t1,topic_t2,topic_tExt,topic_setpoint,topic_presence}; 

const char* topic_rele = "termo/rele";

static bool state = false;

bool toggle_info = false;




byte color_orange[3] = {(byte)232,(byte)157,(byte)19};


///////////////////////////timers//////////////////////////////
static int timer = 0;
static int mils = 0;

static int timergui = 0;
static int milsgui = 0;

void TimerCharge(int seconds)
{
   mils = millis()+ seconds*1000;

}

void TimerGUICharge(int seconds)
{
    
   milsgui = millis()+ seconds*1000;
}

bool TimerCheck()
{
  int decs = 0;
  if (millis() >= mils )
  {
    mils = 0;
    return true;
  }
  else
  {
    return false;
  }
}


bool TimerGUICheck()
{
  int decs = 0;
  if (millis() >= milsgui )
  {
    milsgui = 0;
    return true;
  }
  else
  {
    return false;
  }
}
///////////////////////////////////////////////////////////////////

////////////////mqttcallback/////////////////////////////////////////////////////////
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
    Thermo.t1 = svalue.toFloat();
  }
  else if(strcmp(topic, topic_t2) == 0) 
  {
     Thermo.t2 = svalue.toFloat();
  }
  else if(strcmp(topic, topic_tExt) == 0) 
  {
     Thermo.tExt = svalue.toFloat();
  }
  else if(strcmp(topic, topic_presence) == 0) 
  {
     Thermo.presence = svalue.toInt();
  }
  else if(strcmp(topic, topic_setpoint) == 0) 
  {
     Thermo.set_point_c = svalue.toFloat();
  }
  else
  {
     SetCursor(0,60);
     Thermo.set_point_c = svalue.toFloat();
  }

  
  if(true == Thermo_Logic())
  {
    digitalWrite(RELEPIN,RISCA_ON);
    client.publish(topic_rele,"1");
      
    //DrawRect(240, 0, 40, 40, TFT_ORANGE);
    
  }
  else
  {
    digitalWrite(RELEPIN,RISCA_OFF);
    client.publish(topic_rele,"0");
    //DrawRect(240, 0, 40, 40, (color_blue[0] << 8  | color_blue[1] ) );
  }
  Thermo.connection_state |= CS_NEWDATA;

  GUI_DRAW(false);
   
  
}
/////////////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////


bool Thermo_Logic()
{
  float  temp_to_use = 0;

  if(Thermo.presence == 1)
  {
    temp_to_use = Thermo.t1; //saloto
  }
  else if (Thermo.presence == 2)
  {
    temp_to_use = Thermo.t2; //soffitta
  }
  else if (Thermo.presence == 3)
  {
    temp_to_use = (Thermo.t1 + Thermo.t2)/2;
  }
  else 
  {
    //uknown or all
      temp_to_use = Thermo.t2; //soffitta
  }
  


  if(Thermo.set_point_c > temp_to_use)
  {
     return true;//return DrawRect(20, 60, 200, 10, TFT_ORANGE );
  }
  else
  {
    return false;
  }
}

//////////////////////////////////////////


/////////////////////gui draw////////////////////////////////////



void GUI_DRAW(bool info)
{
  /**
    Salotto Soffitta
  _____

       Setpoint number
  
  */

  tft.setTextColor(TFT_WHITE,TFT_WALL_BLUE); 

  tft.fillScreen(TFT_WALL_BLUE);
  SetCursor(0,0);
  tft.setTextSize(3);
  if(false == info)
  {
    tft.print(  Thermo.t1 ,1  );
  }
  else
  {
    tft.print( "SAL" );
  }
  
  SetCursor(150,0);
  tft.setTextSize(3);
  if(false == info)
  {
    tft.print(  Thermo.t2 ,1  );
  }
  else
  {
    tft.print( "SOF" );
  }

  SetCursor(80,80);
  tft.setTextSize(3);
  tft.setTextColor(TFT_CYAN,TFT_WALL_BLUE); 
  tft.print(   Thermo.set_point_c , 1   );

  if(Thermo.presence == 4)
  {
    //unknown
  }
  else
  {
    if(Thermo.presence & 1) //salotto
    {
      DrawRect(0, 50, 80, 5, TFT_WHITE );
    }
    if(Thermo.presence & 2) //soffitta
    {
       DrawRect(140, 50, 80, 5, TFT_WHITE );
    }
  }

  if(true == Thermo_Logic())
  {
    DrawRect(20, 63, SCREEN_WIDTH-40, 10, TFT_ORANGE );
  }

  //MQTT STATUS...but if i am here it works...
  //DrawRect(0, 120, 40, 20, TFT_GREEN);

   if(Thermo.connection_state & CS_WIFI) //WIFI ON
    {
      DrawRect(3, 128, 10, 10, TFT_WHITE );
    }
    if(Thermo.connection_state & CS_MQTT) //soffitta
    {
       DrawRect(16, 128, 10, 10, TFT_WHITE );
    }


}



//////////////////////////////////////////////////////////////


#define INIT_CON              0
#define INIT_MQTT             1
#define TE_SCREEN_INIT_DRAW   2
#define IDLE_LOOP             3
#define DISCONNECT            4



void loop()
{
  switch(task)
  {
    case INIT_CON :
      WiFi.begin(ssid, password);
      while (WiFi.status() != WL_CONNECTED)
      {
        Thermo.connection_state &= ~CS_WIFI;
        delay(500);
        Serial.println("Connecting to WiFi..");
      }
      Serial.println("Connected to the WiFi network");
      Thermo.connection_state |= CS_WIFI;
      task = INIT_MQTT;
    break;

    case INIT_MQTT:
      client.setServer(mqttServer, mqttPort);
      client.setCallback(callback);

      while (!client.connected()) 
      {
        Thermo.connection_state &= ~CS_MQTT;
        delay(2000);
        Serial.println("Connecting to MQTT...");
        if (client.connect("ESP32Client", mqttUser, mqttPassword )) 
        {
          Serial.println("connected");  
          
          //client.subscribe(topic_t1);
          //client.subscribe(topic_setpoint);

          for(int t = 0; t < MAX_TOPICS; t++)
          {
            client.subscribe(topics[t]);
          }
          Thermo.connection_state |= CS_MQTT;
          task = TE_SCREEN_INIT_DRAW;
          break;
        } 
        else 
        {
          Serial.print("failed with state ");
          Serial.print(client.state());
          delay(2000);
        }
      }
    break;


    case TE_SCREEN_INIT_DRAW:
        tft.fillScreen(TFT_WALL_BLUE);
        SetCursor(0,0);
        tft.setTextSize(2);
        
        TimerCharge(120);
        TimerGUICharge(4);
        Thermo.presence = 2;
        client.publish(topic_presence,"2");
        task = IDLE_LOOP;
    break;

   

    case IDLE_LOOP:
      client.loop();
      //periodic GUI update
      if(true == TimerGUICheck())
      {
        Thermo.connection_state &= ~CS_NEWDATA;
        Serial.println("G");
         if(toggle_info == true)
         {
           GUI_DRAW(true);
          toggle_info = false;
          TimerGUICharge(1);
         }
         else
         {
          GUI_DRAW(false);
          toggle_info = true;
          TimerGUICharge(4);
         }
      }
      
      if(true == TimerCheck()) //
      {
        TimerCharge(120);
        GUI_DRAW(false);
       
        Serial.println("C");
        task = DISCONNECT;
      }
    break;

    case DISCONNECT: //check mqtt
      client.disconnect();
      Thermo.connection_state &= ~CS_MQTT;
      GUI_DRAW(false);
      delay(1000);
      task = INIT_CON;
    break;
    
  }
}
