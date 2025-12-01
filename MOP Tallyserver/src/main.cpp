#include <Arduino.h>
#include <EEPROM.h>
#include <Adafruit_NeoPixel.h>
#include <SPI.h>
#include <Network.h>
#include <EthernetESP32.h>
#include <AsyncTCP.h>
//#include <WebServer.h>
#include <LittleFS.h>
#include <esp_now.h>
#include <WiFi.h>

#include <ATEMbase.h>
#include <ATEMstd.h>

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>


//Define struct for server settings
struct Settings {
    bool uninitialized;
    bool staticIP;
    IPAddress serverIP;
    IPAddress serverSN;
    IPAddress serverGW;
    IPAddress switcherIP;
    
};

Settings settings;


struct TallySettings {
  bool uninitialized;
  uint8_t routing[0x1F];
  uint8_t pixelBrightness;
  uint8_t ledBrightness;

};

TallySettings tallySettings;

// Define the pin where the built-in RGB LED is connected
#define LED_PIN 21

// Define the number of LEDs in the strip (usually 1 for built-in LED)
#define NUM_LEDS 1


// Define pin where Reset-Button is connected
#define RESET_PIN 17
#define RESET_BUTTON_AVAILABLE true
int counter_reset_press = 0;

// W5500 Pin Definitions
#define W5500_CS     14  // Chip Select pin
#define W5500_RST     9  // Reset pin (optional, not used here)
#define W5500_INT    10  // Interrupt pin (optional, not used here)
#define W5500_MISO   12  // MISO pin
#define W5500_MOSI   11  // MOSI pin
#define W5500_SCK    13  // Clock pin

// MAC address (customize if required)
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
W5500Driver driver(W5500_CS, W5500_INT, W5500_RST); // CS pin is 4, INT pin is 26, reset pin is 27
SPIClass SPI1(HSPI);

// Static IP Configuration
IPAddress ip(192, 168, 0, 110);         // Static IP address
IPAddress dns(192, 168, 0, 100);          // DNS server
IPAddress gateway(192, 168, 0, 100);      // Gateway address
IPAddress subnet(255, 255, 255, 0);     // Subnet mask


// ATEM Configuration

IPAddress switcherIp(192, 168, 0, 101);	      // IP address of the ATEM switcher
ATEMstd AtemSwitcher;
int ProgramTally;
int PreviewTally;


#define TALLY_FLAG_OFF                  0
#define TALLY_FLAG_PROGRAM              1
#define TALLY_FLAG_PREVIEW              2
#define TALLY_FLAG_TRANSITION_NEXT      3


#define STATE_OFFLINE                   0
#define STATE_ETHERNET_ONLINE           1
#define STATE_SWITCHER_CONNECTING       2
#define STATE_RUNNING                   3

int state;
bool switcherFirstRun = true;

// ESP NOW WIFI COMMUNICATION

// REPLACE WITH THE RECEIVER'S MAC Address
uint8_t wifi_broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xBD, 0xFD};

typedef struct struct_wifi_message {
    int id; // must be unique for each sender board
    int pixelBrightness;
    int ledBrightness;
    bool tallyPreview[0x1F];
    bool tallyProgram[0x1F];
} struct_wifi_message;

struct_wifi_message atemData;




void makeTallyRouting(int preview, uint32_t program){

  for (int i = 0x00; i <= 0x1F; i++){

    int tallynumber = tallySettings.routing[i];
    if (preview == tallynumber){
      atemData.tallyPreview[i] = 1;
    } else {
      atemData.tallyPreview[i] = 0;
    }
    
    // get k'th bit from program integer
    int pgm = ((program & (1 << (tallynumber - 1))) >> (tallynumber - 1));
    
    if (pgm == 1){
      atemData.tallyProgram[i] = 1;
    } else {
      atemData.tallyProgram[i] = 0;
    }

  }

  atemData.pixelBrightness = tallySettings.pixelBrightness;
  atemData.ledBrightness = tallySettings.ledBrightness;


}

// Create peer interface
esp_now_peer_info_t peerInfo;

// callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if ( status != ESP_NOW_SEND_SUCCESS){
 Serial.print("Last Packet Send Status:\t");
  Serial.println("Delivery Fail");
  }
}

// Create an instance of the Adafruit_NeoPixel class
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

int16_t ledVal = 0;


static AsyncWebServer server(80);
// create websocket handler
static AsyncWebSocketMessageHandler wsHandler;
// add it to the websocket server
static AsyncWebSocket ws("/ws", wsHandler.eventHandler());

void updateWebsocket(){

  JsonDocument doc;

  JsonArray array1 = doc.createNestedArray("preview");
  JsonArray array2 = doc.createNestedArray("program");
  copyArray(atemData.tallyPreview, array1);
  copyArray(atemData.tallyProgram, array2);

  char output[512];
  serializeJson(doc, output);
  ws.printfAll(output);
}



void changeState(int newState){
    state = newState;

}

void initSettings(){
  
  settings.serverIP = IPAddress(192, 168,0, 110);
  settings.serverGW = IPAddress(192,168,0,1);
  settings.serverSN = IPAddress(255, 255, 255, 0);
  settings.uninitialized = false;
  
  
  // Predefine some Tally Routes
  // [Hardware-Adress] = Input Number:
  
  for(int i = 0x01; i <= 0x1F; i++){
    tallySettings.routing[i] = i;
  }
  tallySettings.ledBrightness = 40;
  tallySettings.pixelBrightness = 40;
  tallySettings.uninitialized = false;




  Serial.println("Default settings recovered");

}

void saveSettings(){
  EEPROM.put(0, settings);
  EEPROM.commit();
  Serial.println("Saved settings to EEPROM");
}

void readSettings(){

EEPROM.get(0, settings);

  //Ugly fix for IPAddress not loading correctly when read from EEPROM
settings.serverIP = IPAddress(settings.serverIP[0], settings.serverIP[1], settings.serverIP[2], settings.serverIP[3]);
settings.serverSN = IPAddress(settings.serverSN[0], settings.serverSN[1], settings.serverSN[2], settings.serverSN[3]);
settings.serverGW = IPAddress(settings.serverGW[0], settings.serverGW[1], settings.serverGW[2], settings.serverGW[3]);
settings.switcherIP = IPAddress(settings.switcherIP[0], settings.switcherIP[1], settings.switcherIP[2], settings.switcherIP[3]);


}

void saveTallySettings(){
  EEPROM.put(120, settings);

    int addressIndex = 150;
    for (int i = 1; i <= sizeof(tallySettings); i++) 
    {
      EEPROM.write(addressIndex, tallySettings.routing[i]);
      addressIndex += 1;
    }


  EEPROM.commit();
  Serial.println("Saved tallysettings to EEPROM");
}

void readTallySettings(){

  EEPROM.get(120, tallySettings);
  int addressIndex = 150;
  for (int i = 1; i <= sizeof(tallySettings); i++) 
  {
    tallySettings.routing[i] = EEPROM.read(addressIndex);
    addressIndex += 1;
  }
}

void initializeAllSettings(){
  initSettings();
  saveSettings();
  saveTallySettings();
  readSettings();
  readTallySettings();
}

void evaluateResetPin()
{
  bool stateResetPin = digitalRead(RESET_PIN);
  if(!stateResetPin && RESET_BUTTON_AVAILABLE){
    counter_reset_press++;
    Serial.println("Increase reset press");
  }

  // Set a hint on RGB-LED, that RESET Button is pressed long enough
  if(counter_reset_press > 50){
      strip.setPixelColor(0, strip.Color(0, 10, 10)); // cyan
      strip.show();
  }

  // initialize all Settings if Reset-PIN was pressed for more than 5 seconds and released
  if(counter_reset_press > 50 && stateResetPin){
    Serial.println("Reached init action");

    // let the led blink
    for(int i = 0; i < 10; i++){
      if(i % 2 == 1){   // switch between odd and even
      strip.setPixelColor(0, strip.Color(0, 25, 25)); // cyan
      }
      else {
        strip.setPixelColor(0, strip.Color(25 , 0, 0)); // red
      }
      strip.show();
      delay(200);
    }

    // Reset EEPROM
    initializeAllSettings();
    counter_reset_press = 0;
    Serial.println("SOFT-RESET initialized by RESET-PIN done!");
    Serial.println("ESP restart...");
    ESP.restart();
  }

  if(stateResetPin && RESET_BUTTON_AVAILABLE) {
    counter_reset_press = 0;
  }
}

void setup() {


  changeState(STATE_OFFLINE);

  delay(1000);
  Serial.begin(115200);
  //while (!Serial) {
  //  ; // Wait until serial port is ready
  //}

  Serial.println("Starte LED");
  // Initialize the NeoPixel library
strip.begin();
strip.show(); // Initialize all pixels to 'off'

strip.setPixelColor(0, strip.Color(30 , 0, 0)); // Red
strip.show();

rmtDeinit(RESET_PIN);
pinMode(RESET_PIN, INPUT);

//Read settings from EEPROM. WIFI settings are stored separately by the ESP
EEPROM.begin(300); // Size is 200...

//Reset and initialize EEProm if SOFT-RESET-BUTTON is pressed
while(!digitalRead(RESET_PIN) && RESET_BUTTON_AVAILABLE){
  evaluateResetPin();
  delay(100);
}
evaluateResetPin(); // one more time, for the case, that button is released

//EEPROM.get(0, settings);
readSettings();
readTallySettings();


if(settings.uninitialized || tallySettings.uninitialized){

  Serial.println("initialize tally settings");
  // init settings
  initializeAllSettings();

} 


Serial.println("read settings");


Serial.println("read tally settings");
readTallySettings();
if(!LittleFS.begin(true)){
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }
  
// Initialize SPI with custom pin configuration
  SPI1.begin(W5500_SCK, W5500_MISO, W5500_MOSI, W5500_CS);

  
  driver.setSPI(SPI1);
  driver.setSpiFreq(10);
  driver.setPhyAddress(0);

  Ethernet.init(driver);
    
  // Initialize Ethernet with static IP settings
  Ethernet.begin(mac, settings.serverIP, dns, settings.serverGW, settings.serverSN);

  // Verify if IP address is properly assigned
  if (Ethernet.localIP() == IPAddress(0, 0, 0, 0)) {
    Serial.println("Failed to configure Ethernet with static IP");
    while (true); // Halt on failure
  }

  // Print assigned IP address
  Serial.print("IP Address: ");
  Serial.println(Ethernet.localIP());


  

   server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    root["ip"] = settings.serverIP.toString();
    root["sn"] = settings.serverSN.toString();
    root["gw"] = settings.serverGW.toString();

    root["atemip"] = settings.switcherIP.toString();
    root["espch"] = "6";
  
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(root, *response);

    request->send(response);

 
  });



     server.on("/tallies", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();


    JsonArray inputs = doc.to<JsonArray>();

    for(int i = 1; i <= 0x1F; i++){
      inputs.add(tallySettings.routing[i]);
    }
    root["inputs"] = inputs;
  
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(root, *response);

    request->send(response);

 
  });

    server.on("/settings", HTTP_POST, [](AsyncWebServerRequest *request) {
    
    /*
      // display params
    size_t count = request->params();
    for (size_t i = 0; i < count; i++) {
      const AsyncWebParameter *p = request->getParam(i);
      Serial.printf("PARAM[%u]: %s = %s\n", i, p->name().c_str(), p->value().c_str());
    }
    */
    // get who param
    String newip;
    if (request->hasParam("ip", true)) {
      newip = request->getParam("ip", true)->value();
      settings.serverIP.fromString(newip);
      Serial.println(newip);
    } 

    if (request->hasParam("sn", true)) {
      newip = request->getParam("sn", true)->value();
      settings.serverSN.fromString(newip);
      Serial.println(newip);
    } 

    if (request->hasParam("gw", true)) {
      newip = request->getParam("gw", true)->value();
      settings.serverGW.fromString(newip);
      Serial.println(newip);
    } 


    if (request->hasParam("ch", true)) {
      //channel = request->getParam("ch", true)->value();
      // todo settings..fromString(newip);
     // Serial.println(newip);
    } 

    if(request->hasParam("swip", true)) {
        newip = request->getParam("swip", true)->value();
        settings.switcherIP.fromString(newip);
        Serial.println(newip);
      } 

      saveSettings();

      

    request->send(200, "text/plain", "success");

    delay(500);
    ESP.restart();
  });




      server.on("/brightness", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        root["led"] = tallySettings.ledBrightness;
        root["neopixel"] = tallySettings.pixelBrightness;
      
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        serializeJson(root, *response);

        request->send(response);

    
      });




      server.on("/neopixel", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("brightness", true)) {
            String brightness= request->getParam("brightness", true)->value();
            tallySettings.pixelBrightness = brightness.toInt();
            Serial.print("neopixel brightness updated: ");
            Serial.println(tallySettings.pixelBrightness);
        } 

        request->send(200, "text/plain", "success");
      });
 
      server.on("/led", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("brightness", true)) {
            String brightness= request->getParam("brightness", true)->value();
            tallySettings.ledBrightness = brightness.toInt();
            Serial.print("led brightness updated: ");
            Serial.println(tallySettings.ledBrightness);
          } 

        request->send(200, "text/plain", "success");
      });


    server.on("/tallyroute", HTTP_POST, [](AsyncWebServerRequest *request) {

      if (request->hasParam("input", true) && request->hasParam("tally", true)) {
          String tally= request->getParam("tally", true)->value();
          String input= request->getParam("input", true)->value();
          if(tally.toInt() <= 0x1F && input.toInt() <= 20 ){ // check if input value is valid
            tallySettings.routing[tally.toInt()] = input.toInt();
          }
          
      } 
      request->send(200, "text/plain", "success");
   
    });


    server.on("/tallysave", HTTP_POST, [](AsyncWebServerRequest *request) {

      if (request->hasParam("save", true)) {
        saveTallySettings();
      } 
      request->send(200, "text/plain", "success");
   
    });

    server.on("/*.gz",[](AsyncWebServerRequest *request) {

      request->send(404, "text/plain", "NOT FOUND");
   
    });
    server.serveStatic("/assets", LittleFS, "/assets").setDefaultFile("test.txt");
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // Websocket from here



    wsHandler.onConnect([](AsyncWebSocket *server, AsyncWebSocketClient *client) {
      // do nothing
    });

    wsHandler.onDisconnect([](AsyncWebSocket *server, uint32_t clientId) {
      // do nothing
    });

    wsHandler.onError([](AsyncWebSocket *server, AsyncWebSocketClient *client, uint16_t errorCode, const char *reason, size_t len) {
      // do nothing
    });

    wsHandler.onMessage([](AsyncWebSocket *server, AsyncWebSocketClient *client, const uint8_t *data, size_t len) {
      // do nothing
    });

    wsHandler.onFragment([](AsyncWebSocket *server, AsyncWebSocketClient *client, const AwsFrameInfo *frameInfo, const uint8_t *data, size_t len) {
      // do nothing
    });

  server.addHandler(&ws);

    // Websocket until here

  server.begin();

  // Setup ESP NOW
    // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Once ESPNow is successfully Init, we will register for Send CB to
  // get the status of Trasnmitted packet
  esp_now_register_send_cb(esp_now_send_cb_t(OnDataSent));
  
  // Register peer
  memcpy(peerInfo.peer_addr, wifi_broadcastAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  // Add peer        
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }


 

  delay(100);

  //AtemSwitcher.runLoop();



}

void printBinary(int inByte)
{
  for (int b = 32; b >= 0; b--)
  {
    Serial.print(bitRead(inByte, b));
  }
}

void loopLED(){
  
  switch(state){
    case STATE_OFFLINE:
      strip.setPixelColor(0, strip.Color(ledVal%32 , 0 , 0)); // Red
      break;
    case STATE_ETHERNET_ONLINE:
      strip.setPixelColor(0, strip.Color(ledVal%32 , ledVal%32 , 0)); // Orange
      break;
    case STATE_SWITCHER_CONNECTING:
      strip.setPixelColor(0, strip.Color(0, 0, ledVal%32)); // Blue
      break;
    case STATE_RUNNING:
      strip.setPixelColor(0, strip.Color(0 , ledVal%32 , 0)); // Green
      break;
    
  }
  
  strip.show();
  ledVal++;
}

static uint32_t lastWS = 0;
static uint32_t deltaWS = 2000;

void loopWebsocket(){
  uint32_t now = millis();

  if (now - lastWS >= deltaWS) {
    ws.cleanupClients();
    lastWS = millis();
  }
}



void loop() {

// Check Soft-Reset Button

evaluateResetPin();
if(!digitalRead(RESET_PIN) && RESET_BUTTON_AVAILABLE){
  delay(100);
  return;   // leave loop, if soft-reset button is presset
}


//Serial.println(state);

if (Ethernet.hasIP()){
    if(state == STATE_OFFLINE){
    Serial.println("Ethernet is connected!");
    changeState(STATE_ETHERNET_ONLINE);
    }
} else{
  Serial.println("Ethernet is not connected!");
  changeState(STATE_OFFLINE);
}

if(state == STATE_ETHERNET_ONLINE){
Serial.println("Ethernet is ONLINE 1234!");
// mit dem ATEM verbinden und den Atem abfragen
  if(switcherFirstRun){
      // Initialize a connection to the ATEM switcher:
    Serial.println("Connecting to switcher...........");
    AtemSwitcher.begin(settings.switcherIP);
    //AtemSwitcher.serialOutput(0x80);
    //AtemSwitcher.connect();
    switcherFirstRun = false;

    delay(100);
    AtemSwitcher.runLoop();
    changeState(STATE_SWITCHER_CONNECTING);

    
  }


}

if (state == STATE_SWITCHER_CONNECTING){
  AtemSwitcher.runLoop();
  if (AtemSwitcher.isConnected()) {
      changeState(STATE_RUNNING);
      Serial.println("Connected to switcher");
    }
}


if(state == STATE_RUNNING){


  //Serial.println("AtemSwitcher run loop");
  AtemSwitcher.runLoop();

  if (!AtemSwitcher.isConnected()) {
        changeState(STATE_SWITCHER_CONNECTING);
        Serial.println("Connection to switcher lost!");
  } else {

  //ProgramTally = AtemSwitcher.getProgramInput();
  PreviewTally = AtemSwitcher.getPreviewInput();

  //printBinary(AtemSwitcher.getTallyByFlag(TALLY_FLAG_PREVIEW));
  //Serial.println("pgm: ");
  //printBinary(AtemSwitcher.getTallyByFlag(TALLY_FLAG_PROGRAM));
  uint32_t apgm = AtemSwitcher.getTallyByFlag(TALLY_FLAG_PROGRAM);
  uint32_t atsn = AtemSwitcher.getTallyByFlag(TALLY_FLAG_TRANSITION_NEXT);
  // XOR aus PROGRAM und NEXT gibt die Tallys, die wirklich auf PGM sein sollen
  uint32_t arealpgm = apgm ^ atsn ;
  //Serial.println("The Real Program: ");
  //printBinary(arealpgm);
  //Serial.print(AtemSwitcher.getTallyByIndexTallyFlags(1));

  //Serial.print("ATEM sagt grad: ");
  //Serial.print(ProgramTally);
  //Serial.print(PreviewTally);
  //Serial.print("\n");

  atemData.id = 1;
  atemData.ledBrightness = tallySettings.ledBrightness;
  atemData.pixelBrightness = tallySettings.ledBrightness;
  makeTallyRouting(PreviewTally, arealpgm);


  // Send message via ESP-NOW
  esp_err_t result = esp_now_send(wifi_broadcastAddress, (uint8_t *) &atemData, sizeof(atemData));
    

  // update websocket
  updateWebsocket();

  if (result == ESP_OK) {
    //Serial.println("Sent with success");
  }
  else {
    Serial.println("Error sending the data");
  }
}

loopWebsocket();

}

loopLED();

delay(200);

}



