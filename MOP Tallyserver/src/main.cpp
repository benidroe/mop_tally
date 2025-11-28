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
    uint8_t pixelBrightness;
    uint8_t ledBrightness;
};

Settings settings;


// Define the pin where the built-in RGB LED is connected
#define LED_PIN 21

// Define the number of LEDs in the strip (usually 1 for built-in LED)
#define NUM_LEDS 1

#define LED_BRIGHTNESS 40


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

int tallyRouting[0x1F] = {};  // Tally Routing - Tally-Adresse ---> ATEM Input




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

    int tallynumber = tallyRouting[i];
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

  atemData.pixelBrightness = settings.pixelBrightness;
  atemData.ledBrightness = settings.ledBrightness;


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


void changeState(int newState){
    state = newState;

}

void initSettings(){
  settings.uninitialized = false;
  settings.serverIP = IPAddress(192, 168,0, 110);
  settings.serverGW = IPAddress(192,168,0,1);
  settings.serverSN = IPAddress(255, 255, 255, 0);
  settings.ledBrightness = LED_BRIGHTNESS;
  settings.pixelBrightness = RGB_BRIGHTNESS;
  Serial.println("Default settings recovered");

}

void saveSettings(){
  EEPROM.put(0, settings);
  EEPROM.commit();
  Serial.println("Saved settings to EEPROM");
}

void setup() {



 



  // Predefine some Tally Routes
  // [Hardware-Adress] = Input Number:
  tallyRouting[0x01] = 1;
  tallyRouting[0x02] = 2;
  tallyRouting[0x03] = 3;
  tallyRouting[0x04] = 4;
  tallyRouting[0x05] = 5;
  tallyRouting[0x06] = 6;
  tallyRouting[0x07] = 7;
  tallyRouting[0x08] = 8;
  tallyRouting[0x09] = 9;
  tallyRouting[0x0A] = 10;
  tallyRouting[0x0B] = 11;
  tallyRouting[0x0C] = 12;

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



//Read settings from EEPROM. WIFI settings are stored separately by the ESP
EEPROM.begin(sizeof(settings)); //Needed on ESP8266 module, as EEPROM lib works a bit differently than on a regular Arduino
EEPROM.get(0, settings);


if(settings.uninitialized){

  // init settings
  initSettings();
  saveSettings();

} 

EEPROM.get(0, settings);

Serial.println("read settings");
//Ugly fix for IPAddress not loading correctly when read from EEPROM
settings.serverIP = IPAddress(settings.serverIP[0], settings.serverIP[1], settings.serverIP[2], settings.serverIP[3]);
settings.serverSN = IPAddress(settings.serverSN[0], settings.serverSN[1], settings.serverSN[2], settings.serverSN[3]);
settings.serverGW = IPAddress(settings.serverGW[0], settings.serverGW[1], settings.serverGW[2], settings.serverGW[3]);
settings.switcherIP = IPAddress(settings.switcherIP[0], settings.switcherIP[1], settings.switcherIP[2], settings.switcherIP[3]);
Serial.println(settings.serverIP[0]);



if(!LittleFS.begin(true)){
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }
  
  /*
  File file = LittleFS.open("/test.txt");
  if(!file){
    Serial.println("Failed to open file for reading");
    return;
  }
  
  //Serial.println("File Content:");
  while(file.available()){
    Serial.write(file.read());
  }
  file.close();
  */

 


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

  server.serveStatic("/assets", LittleFS, "/assets").setDefaultFile("test.txt");
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  

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
      inputs.add(tallyRouting[i]);
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

    delay(100);
    ESP.restart();
  });



  server.on("/neopixel", HTTP_POST, [](AsyncWebServerRequest *request) {

  if (request->hasParam("brightness", true)) {
      String brightness= request->getParam("brightness", true)->value();
      settings.pixelBrightness = brightness.toInt();
      Serial.print("neopixel brightness updated: ");
      Serial.println(settings.pixelBrightness);
    } 

    request->send(200, "text/plain", "success");
   


  });
 
  server.on("/led", HTTP_POST, [](AsyncWebServerRequest *request) {

  if (request->hasParam("brightness", true)) {
      String brightness= request->getParam("brightness", true)->value();
      settings.ledBrightness = brightness.toInt();
      Serial.print("led brightness updated: ");
      Serial.println(settings.ledBrightness);
    } 

    request->send(200, "text/plain", "success");
   


  });


    server.on("/tallyroute", HTTP_POST, [](AsyncWebServerRequest *request) {

  if (request->hasParam("input", true) && request->hasParam("tally", true)) {
      String tally= request->getParam("tally", true)->value();
      String input= request->getParam("input", true)->value();
      tallyRouting[tally.toInt()] = input.toInt();
    } 

    request->send(200, "text/plain", "success");
   


  });



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

void loop() {

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
  atemData.ledBrightness = 20;
  atemData.pixelBrightness = 20;
  makeTallyRouting(PreviewTally, arealpgm);


  // Send message via ESP-NOW
  esp_err_t result = esp_now_send(wifi_broadcastAddress, (uint8_t *) &atemData, sizeof(atemData));
    
  if (result == ESP_OK) {
    //Serial.println("Sent with success");
  }
  else {
    Serial.println("Error sending the data");
  }
}

}

loopLED();

delay(200);

}



