/* ********** Rudi the RGB Rendeer **********************
 *  This sketch is used to set up an ESP8266 as SoftAP
 *  and serve a simple Webpage to every connecting client
 *  using a captive Portal.
 *  The Webpage lets us select different animations for
 *  our RGB-LED-Strip-equipped rendeer (or whatever)
 *  
 *  Hardware:
 *  - WEMOS D1 Mini Pro ESP8266 board + prototyping shield
 *  - WS2812B based LED Strip (ebay, china; 5m with 150 LEDs)
 *  - Level Shifter for Data pin (TI CD40109B) -> not really neccessary
 *  - open frame 5V / 20A PSU (ebay, china)
 *  
 *  Code included from various sources:
 *  
 *  Captive Portal:
 *  https://www.hackster.io/rayburne/esp8266-captive-portal-5798ff
 *  
 *  Animations
 *  - FastLED Library samples
 *  - https://github.com/atuline/FastLED-Demos
 */

// Required for use with ESP8266 to stop flickering LEDs
// see https://github.com/FastLED/FastLED/issues/306
// also reduce jitter by WiFi.setSleepMode(WIFI_NONE_SLEEP) in setup
#define FASTLED_INTERRUPT_RETRY_COUNT 0

#include <FastLED.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>

FASTLED_USING_NAMESPACE
#if defined(FASTLED_VERSION) && (FASTLED_VERSION < 3001000)
#warning "Requires FastLED 3.1 or later; check github for latest code."
#endif

// Enable (define) Serial Port for Debugging
//#define SerialEnabled
//unsigned long LoopStartTime;
//float Duration;

// IP Settings
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 1, 1);
DNSServer dnsServer;
ESP8266WebServer webServer(80);
String webPage = "";
String redirectPage = "";

// Active Simulation
// 0 = none (LEDs off)
// 1 = Rainbow
// 2 = Confetti
// 3 = BeatWave
// 4 = BlendWave
// 5 = Snake
// 6 = ColorNoise
// 7 = RedNose
// 10 = White (Full power! - no random-select - ~25W)
int ActiveSim = 7;
#define NumberOfSims 6  // number of simulations (for random simulation selection, "0" will be excluded)

// Status LED on D4
//#define USELED // do not define to disable status LED
#define LED D4
//#define GPIO_TEST D5
// LED inverted (Pull-Up resistor)
#define LEDON LOW
#define LEDOFF HIGH

// Setup RGB LEDs
#define LED_PIN     D8        //D8=GPIO15 -> internal pull-down resistor
#define COLOR_ORDER GRB
#define CHIPSET     WS2812B
#define NUM_LEDS    150
#define BRIGHTNESS  255       //255 = max
#define FRAMES_PER_SECOND 50
#define MAX_MILLI_AMPS  6000  //max. current (for dynamic brightness) -> correction factor 1,3 -> 4000 =~ 3A

// Rudis Anatomy
CRGBArray<NUM_LEDS> leds;
CRGBSet nose(leds(21,25));
CRGBSet eye(leds(27,31));
CRGBSet horns(leds(35,74));
CRGBSet tail(leds(98,102));


// ============== Simulation specific variables =========================
// Confetti
uint8_t  thisfade = 12;      // How quickly does it fade? Lower = slower fade rate.
int       thishue = 50;     // Starting hue.
uint8_t   thisinc = 1;      // Incremental value for rotating hues
uint8_t   thissat = 255;    // The saturation, where 255 = brilliant colours.
uint8_t   thisbri = 255;    // Brightness of a sequence. Remember, max_bright is the overall limiter.
int       huediff = 256;    // Range of random #'s to use for hue

// BeatWave Palette definitions
CRGBPalette16 currentPalette;
CRGBPalette16 targetPalette;
TBlendType    currentBlending;

// BlendWave
CRGB clr1;
CRGB clr2;
uint8_t speed;
uint8_t loc1;
uint8_t loc2;
uint8_t ran1;
uint8_t ran2;

// Snake
#define qsubd(x, b)  ((x>b)?b:0)
#define qsuba(x, b)  ((x>b)?x-b:0)
int8_t thisspeed = 12;                                         // You can change the speed of the wave, and use negative values.
uint8_t allfreq = 32;                                         // You can change the frequency, thus distance between bars.
int thisphase = 0;                                            // Phase change value gets calculated.
uint8_t thiscutoff = 128;                                     // You can change the cutoff value to display this wave. Lower value = longer wave.
int thisdelay = 30;                                           // You can change the delay. Also you can change the allspeed variable above. 
uint8_t bgclr = 0;                                            // A rotating background colour.
uint8_t bgbright = 0;                                        // Brightness of background colour
CRGBPalette16 scurrentPalette;
CRGBPalette16 stargetPalette;

// Color noise
static int16_t dist;                                          // A random number for our noise generator.
uint16_t xscale = 30;                                         // Wouldn't recommend changing this on the fly, or the animation will be really blocky.
uint16_t yscale = 30;                                         // Wouldn't recommend changing this on the fly, or the animation will be really blocky.


// Automatic Simulation switching and minumum sim runtime
const unsigned long MinWebRuntime = 2 * 1000;        // Simulation runs at least 2 seconds before it can be changed manually
unsigned long NextWebSimSwitch = 0;
const unsigned long AutoSimSwitch = 300 * 1000;       // Simulations automatically rotate after 5 minutes (random order)
unsigned long NextAutoSimSwitch = 300000;

/*
 * Setup
 * ========================================================================
 */
void setup() {
  delay(1000);          //Startup delay
  // start serial port and digital Outputs
  #ifdef SerialEnabled
  Serial.begin(115200);
  Serial.println();
  Serial.println();
  Serial.println("ESP8266 RGB-Rudi Controller");
  #endif
  #ifdef USELED
  pinMode(LED, OUTPUT);
  //pinMode(GPIO_TEST, OUTPUT);
  digitalWrite(LED, LEDOFF);
  ToggleLed(LED,200,6);
  #endif

  // Init random number generator
  randomSeed(analogRead(0));
  
  // Setup Access Point
  #ifdef SerialEnabled
  Serial.println("Setup AccessPoint..");
  #endif
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("Rudi");
  // Disable WIFI Sleep to reduce LED jitter
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  // DNS Server
  #ifdef SerialEnabled
  Serial.println("Setup DNS Server..");
  #endif
  dnsServer.setTTL(60);
  dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure);
  // start DNS server, respond with ESP IP on all requests
  dnsServer.start(DNS_PORT, "*", apIP);

  // Web Server functions and content
  // Web Page to serve
  webPage += "<center><h1>Rudi das RGB Rentier</h1><br>";
  webPage += "<p><a href=\"/led?sim=Rainbow\"><button style=\"height:50px;width:200px;font-size:20px\">Regenbogen</button></a></p><br>";
  webPage += "<p><a href=\"/led?sim=Confetti\"><button style=\"height:50px;width:200px;font-size:20px\">Konfetti</button></a></p><br>";
  webPage += "<p><a href=\"/led?sim=BeatWave\"><button style=\"height:50px;width:200px;font-size:20px\">Wellen 1</button></a></p><br>";
  webPage += "<p><a href=\"/led?sim=BlendWave\"><button style=\"height:50px;width:200px;font-size:20px\">Wellen 2</button></a></p><br>";
  webPage += "<p><a href=\"/led?sim=Snake\"><button style=\"height:50px;width:200px;font-size:20px\">Schlangen</button></a></p><br>";
  webPage += "<p><a href=\"/led?sim=RedNose\"><button style=\"height:50px;width:200px;font-size:20px\">Rote Nase</button></a></p><br>";
  webPage += "<p><a href=\"/led?sim=ColorNoise\"><button style=\"height:50px;width:200px;font-size:20px\">Bunt</button></a></p><br></center>";
  //webPage += "<p><a href=\"/led?sim=Off\"><button style=\"height:50px;width:200px;font-size:20px\">Aus</button></a></p><br>";
  //webPage += "<p><a href=\"/led?sim=White\"><button style=\"height:50px;width:200px;font-size:20px\">Weiss</button></a></p><br></center>";

  // Redirect unknown requests to web Page
  redirectPage += "<head><meta HTTP-EQUIV=\"REFRESH\" content=\"0; url=http://rudi.rentier/\"</head><body></body>";
  
  webServer.onNotFound([]() {
    webServer.send(200, "text/html", redirectPage);
  });
  
  webServer.on("/", [](){
    #ifdef SerialEnabled
    Serial.println("Web: Serve start page");
    #endif
    webServer.send(200, "text/html", webPage);
  });
  webServer.on("/led", []() {
    String SimString = webServer.arg("sim");
    if (millis() > NextWebSimSwitch) {
      if (SimString == "Off") ActiveSim = 0;
      else if (SimString == "Rainbow") ActiveSim = 1;
      else if (SimString == "Confetti") ActiveSim = 2;
      else if (SimString == "BeatWave") ActiveSim = 3;
      else if (SimString == "BlendWave") ActiveSim = 4;
      else if (SimString == "Snake") ActiveSim = 5;
      else if (SimString == "ColorNoise") ActiveSim = 6;
      else if (SimString == "RedNose") ActiveSim = 7;
      else if (SimString == "White") ActiveSim = 10;
      #ifdef SerialEnabled
      Serial.println("Web: Simulation set to " + SimString);
      #endif
      NextWebSimSwitch = millis() + MinWebRuntime;
      NextAutoSimSwitch = millis() + AutoSimSwitch; // also reset auto-sim-switch counter
    }
    else {
      #ifdef SerialEnabled
      Serial.println("Web: Simulation change timeout not reached.");
      #endif
      delay(1);
    }
      webServer.send(200, "text/html", webPage);
  });
  /*webServer.on("/debug", []() {
    String DbgString = webServer.arg("GT");
    if (DbgString == "Off") digitalWrite(GPIO_TEST,LOW);
    else if (DbgString == "On") digitalWrite(GPIO_TEST,HIGH);
    #ifdef SerialEnabled
    Serial.print("Web: Debug GPIO set to ");
    Serial.println(DbgString);
    #endif
    webServer.send(200, "text/html", webPage);
  });*/
  #ifdef SerialEnabled
  Serial.println("Start Webserver..");
  #endif
  webServer.begin();

  // Setup LED Strip
  #ifdef SerialEnabled
  Serial.println("Setup LED strip..");
  #endif
  FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness( BRIGHTNESS );
  set_max_power_in_volts_and_milliamps(5,MAX_MILLI_AMPS);               // FastLED 3.1 Power management

  // BeatWave Setup
  currentPalette = RainbowColors_p;
  currentBlending = LINEARBLEND;
  // Snake Setup
  scurrentPalette = LavaColors_p;
  // ColorNoise Setup
  dist = random16(12345);

}


/*
 * Main Loop
 * ========================================================================
 */
void loop() {
  //LoopStartTime = millis(); // Debug for measuring loop time
  #ifdef USELED
  ToggleLed(LED,1,1);
  #endif
  dnsServer.processNextRequest();       // Process DNS request
  webServer.handleClient();             // Process HTTP request
  if (millis() > NextAutoSimSwitch) {   // Automatic Simulation switching
    ActiveSim = random(1, NumberOfSims);
    NextAutoSimSwitch = millis() + AutoSimSwitch;
    #ifdef SerialEnabled
    Serial.println("AutoSimSwitch triggered, new sim: " + String(ActiveSim));
    #endif   
  }
  switch (ActiveSim) {                  // Run the selected simulation
  case 0:
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    break;
  case 10:
    fill_solid(leds, NUM_LEDS, CRGB::White);
    break;
  case 1:
    rainbowWithGlitter();
    break;
  case 2:
    confetti();
  break;
  case 3:
    beatwave(); // run beat_wave simulation frame
    EVERY_N_MILLISECONDS(300) {
      nblendPaletteTowardPalette(currentPalette, targetPalette, 24);
    }
    // Change the target palette to a random one every 5 seconds.
    EVERY_N_SECONDS(3) {
      targetPalette = CRGBPalette16(CHSV(random8(), 255, random8(128,255)), CHSV(random8(), 255, random8(128,255)), CHSV(random8(), 192, random8(128,255)), CHSV(random8(), 255, random8(128,255)));
    }
  break;
  case 4:
    blendwave(); // run BlendWave simulation frame
  break;
  case 5:
    one_sine_pal(millis()>>4);
    EVERY_N_MILLISECONDS(100) {
    nblendPaletteTowardPalette(scurrentPalette, stargetPalette, 24);
    }
    EVERY_N_SECONDS(2) {
    static uint8_t baseC = random8();
    stargetPalette = CRGBPalette16(CHSV(random8(), 255, random8(128,255)), CHSV(random8(), 255, random8(128,255)), CHSV(random8(), 192, random8(128,255)), CHSV(random8(), 255, random8(128,255)));
    }
  break;
  case 6:
    nblendPaletteTowardPalette(currentPalette, targetPalette, 64);
    fillnoise8();
    EVERY_N_SECONDS(2) {
      targetPalette = CRGBPalette16(CHSV(random8(), 255, random8(128,255)), CHSV(random8(), 255, random8(128,255)), CHSV(random8(), 192, random8(128,255)), CHSV(random8(), 255, random8(128,255)));
    }
  break;
  case 7:
    fill_rainbow(leds, NUM_LEDS, millis()/15);  // fill strip with moving rainbow.
    nose = CRGB::Red;
    eye = CRGB::Blue;
    horns = CRGB::LightSalmon;
    tail = CRGB::White;
  break;
  }
  show_at_max_brightness_for_power(); // display frame with max brightness for given power budget
  FastLED.delay(1000 / FRAMES_PER_SECOND);
}


/*
 *        FastLED Functions
 * ========================================================================
 */
void rainbowWithGlitter()
{
  uint8_t beatA = beatsin8(17, 0, 255);                        // Starting hue
  uint8_t beatB = beatsin8(13, 0, 255);
  fill_rainbow(leds, NUM_LEDS, (beatA+beatB)/2, 8);            // Use FastLED's fill_rainbow routine.
  addGlitter(100);
}

void addGlitter( fract8 chanceOfGlitter) 
{
  if( random8() < chanceOfGlitter) {
    leds[ random16(NUM_LEDS) ] += CRGB::White;
  }
}

void confetti() {                                             // random colored speckles that blink in and fade smoothly
  uint8_t secondHand = (millis() / 1000) % 30;                // IMPORTANT!!! Change '15' to a different value to change duration of the loop.
  static uint8_t lastSecond = 99;                             // Static variable, means it's only defined once. This is our 'debounce' variable.
  if (lastSecond != secondHand) {                             // Debounce to make sure we're not repeating an assignment.
    lastSecond = secondHand;
    switch(secondHand) {
      case  0: thisinc=1; thishue=192; thissat=255; thisfade=2; huediff=256; break;  // You can change values here, one at a time , or altogether.
      case  5: thisinc=2; thishue=128; thisfade=8; huediff=64; break;
      case 10: thisinc=1; thishue=random16(255); thisfade=1; huediff=16; break;      // Only gets called once, and not continuously for the next several seconds. Therefore, no rainbows.
      case 15: break;                                                                // Here's the matching 15 for the other one.
    }
  }
  fadeToBlackBy(leds, NUM_LEDS, thisfade);                    // Low values = slower fade.
  int pos = random16(NUM_LEDS);                               // Pick an LED at random.
  leds[pos] += CHSV((thishue + random16(huediff))/4 , thissat, thisbri);  // I use 12 bits for hue so that the hue increment isn't too quick.
  thishue = thishue + thisinc;                                // It increments here.
}

void beatwave() {
  
  uint8_t wave1 = beatsin8(9, 0, 255);                        // That's the same as beatsin8(9);
  uint8_t wave2 = beatsin8(8, 0, 255);
  uint8_t wave3 = beatsin8(7, 0, 255);
  uint8_t wave4 = beatsin8(6, 0, 255);

  for (int i=0; i<NUM_LEDS; i++) {
    leds[i] = ColorFromPalette( currentPalette, i+wave1+wave2+wave3+wave4, 255, currentBlending); 
  }
  
}

void blendwave() {

  speed = beatsin8(6,0,255);

  clr1 = blend(CHSV(beatsin8(3,0,255),255,255), CHSV(beatsin8(4,0,255),255,255), speed);
  clr2 = blend(CHSV(beatsin8(4,0,255),255,255), CHSV(beatsin8(3,0,255),255,255), speed);

  loc1 = beatsin8(10,0,NUM_LEDS-1);
  
  fill_gradient_RGB(leds, 0, clr2, loc1, clr1);
  fill_gradient_RGB(leds, loc1, clr2, NUM_LEDS-1, clr1);

}

void one_sine_pal(uint8_t colorIndex) {                                       // This is the heart of this program. Sure is short.
  
  thisphase += thisspeed;                                                     // You can change direction and speed individually.
  
  for (int k=0; k<NUM_LEDS-1; k++) {                                          // For each of the LED's in the strand, set a brightness based on a wave as follows:
    int thisbright = qsubd(cubicwave8((k*allfreq)+thisphase), thiscutoff);    // qsub sets a minimum value called thiscutoff. If < thiscutoff, then bright = 0. Otherwise, bright = 128 (as defined in qsub)..
    leds[k] = CHSV(bgclr, 255, bgbright);                                     // First set a background colour, but fully saturated.
    leds[k] += ColorFromPalette( currentPalette, colorIndex, thisbright, currentBlending);    // Let's now add the foreground colour.
    colorIndex +=3;
  }
  
  bgclr++;
  
}

void fillnoise8() {
  
  for(int i = 0; i < NUM_LEDS; i++) {                                      // Just ONE loop to fill up the LED array as all of the pixels change.
    uint8_t index = inoise8(i*xscale, dist+i*yscale) % 255;                // Get a value from the noise function. I'm using both x and y axis.
    leds[i] = ColorFromPalette(currentPalette, index, 255, LINEARBLEND);   // With that value, look up the 8 bit colour palette value and assign it to the current LED.
  }
  
  dist += beatsin8(10,1,4);                                                // Moving along the distance (that random number we started out with). Vary it a bit with a sine wave.
                                                                           // In some sketches, I've used millis() instead of an incremented counter. Works a treat.
}


/*
 * Common Functions
 * ========================================================================
 */
void ToggleLed (int PIN,int WaitTime,int Count)
{
  // Toggle digital output
  for (int i=0; i < Count; i++)
  {
   digitalWrite(PIN, !digitalRead(PIN));
   delay(WaitTime); 
  }
}
