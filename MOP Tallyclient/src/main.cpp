#include <Arduino.h>
//#include <Adafruit_NeoPixel.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <FastLED.h>



// ESP8266

// Define Tally Hardware Address

#define TALLY_HWAddr 0x0A    // Hardwareadress as hex - Range 0x00 - 0x1F

// Define LED-PINs
#define PIN_RED1    D4 // D0
#define PIN_GREEN1  D5 // D2
#define PIN_BLUE1   D6  // D1

// Define the Neopixel
#define LED_PIN 13
#define FASTLED_ALL_PINS_HARDWARE_SPI

// Define the number of LEDs in the strip (usually 1 for built-in LED)
#define NUM_LEDS 16
CRGB leds[NUM_LEDS];

#define TALLY_FLAG_OFF                  0
#define TALLY_FLAG_PROGRAM              1
#define TALLY_FLAG_PREVIEW              2

int tally_id = 1;

int32 tally_last_update = 10;

//Define LED colors
#define LED_OFF     0
#define LED_RED     1
#define LED_GREEN   2
#define LED_BLUE    3
#define LED_YELLOW  4
#define LED_PINK    5
#define LED_WHITE   6
#define LED_ORANGE  7


// Create an instance of the Adafruit_NeoPixel class
//Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Set LED Brightness
int pixelBrightness = 20;
int ledBrightness = 20;

#define OFFLINE false;
#define ONLINE true;
bool connectionstate = OFFLINE;

u_int32_t colorCode(int color) {

  switch (color) {
      case LED_OFF:
         return strtol("0x000000", NULL, 0);
        break;
      case LED_RED:
          return strtol("0xFF0000", NULL, 0);
        break;
      case LED_GREEN:
          return strtol("0x00FF00", NULL, 0);
        break;
      case LED_BLUE:
          return strtol("0x0000FF", NULL, 0);
        break;
      case LED_ORANGE:
          return strtol("0xFFFF00", NULL, 0);
        break;
      case LED_PINK:
          return strtol("0xFF00FF", NULL, 0);
        break;
      case LED_WHITE:
          return strtol("0xFFFFFF", NULL, 0);
        break;
      default:
        return strtol("0x000000", NULL, 0);
        break; // Wird nicht benötigt, wenn Statement(s) vorhanden sind
    }

}

void setLEDStripColor(int color){

  u_int32_t color32 = colorCode(color);
  FastLED.setBrightness(pixelBrightness);
  for(int i = 0; i < NUM_LEDS; i++){
    leds[i] = color32;
  }
  
  FastLED.show();

}

void setLEDColor(int color) {
  // configure LED Pins
  int pinRed = PIN_RED1;
  int pinGreen = PIN_GREEN1;
  int pinBlue = PIN_BLUE1;

  switch (color) {
        case LED_OFF:
            analogWrite(pinRed, 0);
            analogWrite(pinGreen, 0);
            analogWrite(pinBlue, 0);
            break;
        case LED_RED:
            analogWrite(pinRed, ledBrightness);
            analogWrite(pinGreen, 0);
            analogWrite(pinBlue, 0);
            break;
        case LED_GREEN:
            analogWrite(pinRed, 0);
            analogWrite(pinGreen, ledBrightness);
            analogWrite(pinBlue, 0);
            break;
        case LED_BLUE:
            analogWrite(pinRed, 0);
            analogWrite(pinGreen, 0);
            analogWrite(pinBlue, ledBrightness);
            break;
        case LED_ORANGE:
            analogWrite(pinRed, ledBrightness);
            analogWrite(pinGreen, ledBrightness);
            analogWrite(pinBlue, 0);
            break;
        case LED_PINK:
            analogWrite(pinRed, ledBrightness);
            analogWrite(pinGreen, 0);
            analogWrite(pinBlue, ledBrightness);
            break;
        case LED_WHITE:
            analogWrite(pinRed, ledBrightness);
            analogWrite(pinGreen, ledBrightness);
            analogWrite(pinBlue, ledBrightness);
            break;
    }
}


typedef struct struct_wifi_message {
    int id; // must be unique for each sender board
    int pixelBrightness;
    int ledBrightness;
    bool tallyPreview[0x1F];
    bool tallyProgram[0x1F];
} struct_wifi_message;

struct_wifi_message atemData;

int getTallyStateColor(){
  if (tally_last_update > 10){
     return LED_BLUE;         // Im Falle eines Timeouts wird das Tally blau
  } else if ( atemData.tallyProgram[TALLY_HWAddr] == tally_id){
    return LED_RED;
  } else if(atemData.tallyPreview[TALLY_HWAddr] == tally_id){
    return LED_GREEN;
  } 
  return LED_OFF;
}

int oldTallyStateColor = LED_ORANGE;
//callback function that will be executed when data is received
void OnDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
  memcpy(&atemData, incomingData, sizeof(atemData));
  Serial.print("Bytes received ");
  Serial.print(atemData.tallyPreview[TALLY_HWAddr]);
  Serial.print(atemData.tallyProgram[TALLY_HWAddr]);
  tally_last_update = 0;
  ledBrightness = atemData.ledBrightness;
  pixelBrightness = atemData.pixelBrightness;
  setLEDColor(getTallyStateColor());

  setLEDStripColor(getTallyStateColor());
  oldTallyStateColor = getTallyStateColor();
  connectionstate = ONLINE;
  

  
}

void setup() {
  delay(1000);
  Serial.begin(115200);
  //while (!Serial) {
    //; // Wait until serial port is ready
  //}
  Serial.println("MOP Tally client starting");


   //Init pins for LED
    pinMode(PIN_RED1, OUTPUT);
    pinMode(PIN_GREEN1, OUTPUT);
    pinMode(PIN_BLUE1, OUTPUT);


    delay(500);
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
    // Init Neopixel
    //strip.begin();
    //strip.show(); // Initialize all pixels to 'off'
    FastLED.setBrightness(pixelBrightness);
    setLEDColor(LED_ORANGE);
    setLEDStripColor(LED_ORANGE);
    delay (100);
    setLEDStripColor(LED_ORANGE);
    oldTallyStateColor = LED_ORANGE;
    

    Serial.println("Starting ESP-NOW receiver");
    //Set device as a Wi-Fi Station
    WiFi.mode(WIFI_STA);

    //Init ESP-NOW
    if (esp_now_init() != 0) {
      Serial.println("Error initializing ESP-NOW");
      return;
    }
    
    // Once ESPNow is successfully Init, we will register for recv CB to
    // get recv packer info
    esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
    esp_now_register_recv_cb(OnDataRecv);

}



void loop() {
  // put your main code here, to run repeatedly:

  tally_last_update ++;

  if(tally_last_update > 10 && connectionstate) {
    connectionstate = OFFLINE;
    setLEDColor(LED_BLUE);
    setLEDStripColor(LED_BLUE);
    oldTallyStateColor = LED_BLUE;
  }
  
  delay(100);
}




