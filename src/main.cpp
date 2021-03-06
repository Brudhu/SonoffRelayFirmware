/*
   1MB flash sizee

   sonoff header
   1 - vcc 3v3
   2 - rx
   3 - tx
   4 - gnd
   5 - gpio 14

   esp8266 connections
   gpio  0 - button
   gpio 12 - relay
   gpio 13 - green led - active low
   gpio 14 - pin 5 on header

*/

#define SONOFF_BUTTON    0
#define SONOFF_RELAY    12
#define SONOFF_LED      13
#define SONOFF_INPUT    14

#include <ESP8266WiFi.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <EEPROM.h>

#define EEPROM_SALT 12663
typedef struct {
  int   salt = EEPROM_SALT;
} WMSettings;

WMSettings settings;

#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>

WiFiServer server(80);
WiFiServer serverTcp(2211);
WiFiClient client;
WiFiClient clientTcp;

//for LED status
#include <Ticker.h>
Ticker ticker;
Ticker tickerOTA;
Ticker tickerTCP;
Ticker tickerPB;

bool ota = 0;
bool pb = 0;
bool tcp = 0;

const int CMD_WAIT = 0;
const int CMD_BUTTON_CHANGE = 1;

int cmd = CMD_WAIT;
int relayState = HIGH;

//inverted button state
int buttonState = HIGH;

static long startPress = 0;

String inputString = "";         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete

void tick()
{
  //toggle state
  int state = digitalRead(SONOFF_LED);  // get the current state of GPIO1 pin
  digitalWrite(SONOFF_LED, !state);     // set pin to the opposite state
}

void tickOTA()
{
  ota = 1;
}

void tickTCP()
{
  tcp = 1;
}

void tickPB()
{
  pb = 1;
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}


void setState(int s) {
  digitalWrite(SONOFF_RELAY, s);
  digitalWrite(SONOFF_LED, (s + 1) % 2); // led is active low
}

void turnOn() {
  relayState = HIGH;
  setState(relayState);
}

void turnOff() {
  relayState = LOW;
  setState(relayState);
}

void toggleState() {
  cmd = CMD_BUTTON_CHANGE;
}

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


void toggle() {
  Serial.println("toggle state");
  Serial.println(relayState);
  relayState = relayState == HIGH ? LOW : HIGH;
  setState(relayState);
}

void restart() {
  ESP.reset();
  delay(1000);
}

void reset() {
  //reset settings to defaults
  /*
    WMSettings defaults;
    settings = defaults;
    EEPROM.begin(512);
    EEPROM.put(0, settings);
    EEPROM.end();
  */
  //reset wifi credentials
  WiFi.disconnect();
  delay(1000);
  ESP.reset();
  delay(1000);
}

void setup()
{
  Serial.begin(115200);
  
  inputString.reserve(200);

  //set led pin as output
  pinMode(SONOFF_LED, OUTPUT);
  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);


  const char *hostname = "ESP8266";

  WiFiManager wifiManager;
  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);

  //timeout - this will quit WiFiManager if it's not configured in 3 minutes, causing a restart
  wifiManager.setConfigPortalTimeout(180);

  //custom params
  EEPROM.begin(512);
  EEPROM.get(0, settings);
  EEPROM.end();

  if (settings.salt != EEPROM_SALT) {
    Serial.println("Invalid settings in EEPROM, trying with defaults");
    WMSettings defaults;
    settings = defaults;
  }

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  if (!wifiManager.autoConnect(hostname)) {
    Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("Saving config");

    EEPROM.begin(512);
    EEPROM.put(0, settings);
    EEPROM.end();
  }

  //OTA
  ArduinoOTA.onStart([]() {
    //Serial.println("Start OTA");
  });
  ArduinoOTA.onEnd([]() {
    //Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  
  //ArduinoOTA.setPassword((const char *)"123456");
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.begin();

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");
  ticker.detach();
  
  // Start the server
  server.begin();
  serverTcp.begin();
  Serial.println("Server started");
  
  // Print the IP address
  Serial.print("Use this URL to connect: ");
  Serial.print("http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");

  //setup button
  pinMode(SONOFF_BUTTON, INPUT);
  attachInterrupt(SONOFF_BUTTON, toggleState, CHANGE);

  //setup relay
  pinMode(SONOFF_RELAY, OUTPUT);

  turnOff();
  Serial.println("done setup");
  
  tickerOTA.attach(0.1, tickOTA);
  tickerTCP.attach(0.05, tickTCP);
  tickerPB.attach(0.1, tickPB);
  
  /*noInterrupts();
  timer0_isr_init();
  timer0_attachInterrupt(timer0_ISR);
  timer0_write(ESP.getCycleCount() + 8000000L); // 8MHz == 0.1sec
  interrupts();*/
}

/*void timer0_ISR (void) 
{
  //ota loop
  timer0_write(ESP.getCycleCount() + 8000000L); // 80MHz == 0.1sec
}*/

void loop()
{
  delay(40);
  
  if(ota)
  {
    ota = 0;
    ArduinoOTA.handle();
  }

  if(tcp)
  {
    tcp = 0;
    client = server.available();
    
    if(!clientTcp.connected())
      clientTcp = serverTcp.available();

    delay(10);
    
    if(client.available())
    {
      // Read the first line of the request
      String request = client.readStringUntil('\r');
      Serial.println(request);
      client.flush();

      int value = LOW;
      bool valid = false;
      if (request.indexOf("/DO0=0") != -1)  {
        turnOff();
        value = LOW;
        valid = true;
      }   
      else if (request.indexOf("/DO0=1") != -1)  {
        turnOn();
        value = HIGH;
        valid = true;
      }

      // Return the response
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/html");
      client.println(""); //  do not forget this one
      client.println("<!DOCTYPE HTML>");
      client.println("<html>");

      client.print("DO0=");
 
      if(relayState == HIGH) {
        client.print("1");
      } else {
        client.print("0");
      }
      client.println("<br><br>");
      client.println("<a href=\"/DO0=1\"\"><button>Turn On </button></a>");
      client.println("<a href=\"/DO0=0\"\"><button>Turn Off </button></a><br />");  
      client.println("</html>");
    }
    
    if(clientTcp.available())
    {
      String request = clientTcp.readStringUntil('\n');
      //Serial.println(request);
      clientTcp.flush();
        
      if (request.indexOf("DO0=0") != -1)  {
        turnOff();
        clientTcp.println("OK");
      }   
      else if (request.indexOf("DO0=1") != -1)  {
        turnOn();
        clientTcp.println("OK");
      }   
      
      else if (request.indexOf("DO0?") != -1)  {
        clientTcp.print("DO0=");
        if(relayState == HIGH) {
          clientTcp.println("1");
        } else {
          clientTcp.println("0");
        }
      }   
    }
  }

  if(pb)
  {
    pb = 0;
    switch (cmd) {
      case CMD_WAIT:
        break;
      case CMD_BUTTON_CHANGE:
        int currentState = digitalRead(SONOFF_BUTTON);
        if (currentState != buttonState) {
          if (buttonState == LOW && currentState == HIGH) {
            long duration = millis() - startPress;
            if (duration < 1000) {
              //Serial.println("short press - toggle relay");
              toggle();
            } else if (duration < 5000) {
              //Serial.println("medium press - reset");
              restart();
            } else if (duration < 60000) {
              //Serial.println("long press - reset settings");
              reset();
            }
          } else if (buttonState == HIGH && currentState == LOW) {
            startPress = millis();
          }
          buttonState = currentState;
        }
        break;
    }
  }

}
