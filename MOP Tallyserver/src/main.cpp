#include <Arduino.h>
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
  Ethernet.begin(mac, ip, dns, gateway, subnet);

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

  Serial.println(state);

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
    AtemSwitcher.begin(switcherIp);
    AtemSwitcher.serialOutput(0x80);
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


   Serial.println("AtemSwitcher run loop");
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
  Serial.println("The Real Program: ");
  printBinary(arealpgm);
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
    Serial.println("Sent with success");
  }
  else {
    Serial.println("Error sending the data");
  }
}

}

loopLED();

delay(200);

}



