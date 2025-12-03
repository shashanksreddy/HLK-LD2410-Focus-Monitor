/*
 * HLK-LD2410 & ESP32 mmWave Radar "Focus Detector"
 * FIXED VERSION
 * * Fixes:
 * 1. Removed blocking delay() which was breaking Serial communication.
 * 2. Implemented millis() timer for update intervals.
 * 3. Ensures radar.read() is called every loop cycle.
 */

// --- Libraries ---
#include <ld2410.h>
#include <Adafruit_NeoPixel.h>

// --- Sensor Pins ---
#define MONITOR_SERIAL Serial
#define RADAR_SERIAL Serial1
#define RADAR_RX_PIN 16
#define RADAR_TX_PIN 17

// --- LED Ring Pins & Config ---
#define LED_PIN 23
#define LED_COUNT 8
#define LED_BRIGHTNESS_MAX 255 
#define LED_BRIGHTNESS_BASE 50 

// --- Focus Logic Settings ---
#define DISTRACTION_ENERGY_THRESHOLD 30 
#define CHECK_DELAY 250                 
#define HISTORY_LENGTH 10               

// --- Proximity Alert Zone Settings ---
#define FOCUS_AREA_DISTANCE 100         
#define ZONE_RED 150                    
#define ZONE_YELLOW 200                 
#define ZONE_BLUE 300                   

// --- Break Reminder Settings ---
#define FOCUS_TIME_LIMIT_MS (52L * 60L * 1000L) 
#define BREAK_TIME_MS (17L * 60L * 1000L)      
#define BRIGHTNESS_STEP 20 

// --- Color Definitions ---
#define COLOR_R_RED 255
#define COLOR_G_RED 0
#define COLOR_B_RED 0

#define COLOR_R_YELLOW 255
#define COLOR_G_YELLOW 165
#define COLOR_B_YELLOW 0

#define COLOR_R_BLUE 0
#define COLOR_G_BLUE 0
#define COLOR_B_BLUE 255

#define COLOR_R_GREEN 0
#define COLOR_G_GREEN 255
#define COLOR_B_GREEN 0

#define COLOR_R_PURPLE 128 
#define COLOR_G_PURPLE 0
#define COLOR_B_PURPLE 128

#define COLOR_R_OFF 0
#define COLOR_G_OFF 0
#define COLOR_B_OFF 0

// --- Global Variables ---
int energyHistory[HISTORY_LENGTH];
long totalEnergy = 0;
int historyIndex = 0;

uint32_t currentColor = 0; 
uint32_t targetColor = 0; 
int focusLevel = 0;       

unsigned long focusStartTime = 0;
bool breakNeeded = false;
unsigned long lastStateChange = 0; 

// --- Timing Variable (The Fix) ---
unsigned long lastCheckTime = 0;

// --- Objects ---
ld2410 radar;
Adafruit_NeoPixel ring = Adafruit_NeoPixel(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// --- Function Prototypes ---
void animateRingToColor(uint8_t targetR, uint8_t targetG, uint8_t targetB, bool isFading, uint8_t brightness = LED_BRIGHTNESS_BASE);
void setRingColorImmediate(uint32_t color);

void setup(void)
{
  MONITOR_SERIAL.begin(115200);
  // ESP32 RX pin (16) goes to LD2410 TX. 
  // ESP32 TX pin (17) goes to LD2410 RX.
  RADAR_SERIAL.begin(256000, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);

  ring.begin();
  ring.setBrightness(LED_BRIGHTNESS_BASE); 
  ring.show();

  for (int i = 0; i < HISTORY_LENGTH; i++) {
    energyHistory[i] = 0;
  }
  
  delay(1000); // Allow sensor to boot
  MONITOR_SERIAL.println(F("\nRadar Focus/Proximity Project: Initializing..."));

  if (radar.begin(RADAR_SERIAL))
  {
    MONITOR_SERIAL.println(F("Radar sensor connected OK."));
    // Set max range to Gate 4 (approx 3 meters)
    radar.setMaxValues(4, 4, 0);
  }
  else
  {
    MONITOR_SERIAL.println(F("Radar sensor NOT connected! Check Wiring (TX<->RX)."));
  }

  currentColor = ring.Color(COLOR_R_OFF, COLOR_G_OFF, COLOR_B_OFF);
  targetColor = currentColor;
}

void loop()
{
  // 1. CRITICAL: Read radar data EVERY loop cycle. Do not delay here.
  radar.read();

  // 2. Perform logic only every CHECK_DELAY (250ms)
  if (millis() - lastCheckTime >= CHECK_DELAY) {
    lastCheckTime = millis();
    
    // Check if break is over
    if (breakNeeded && (millis() - lastStateChange) > BREAK_TIME_MS) {
        breakNeeded = false;
        focusStartTime = millis();
        MONITOR_SERIAL.println(F("Break time over. Focus timer restarted."));
    }

    // --- PROXIMITY ALERT (Priority 1) ---
    // Note: isConnected() only updates if read() is successful
    if (!radar.isConnected()) {
        MONITOR_SERIAL.println("Radar disconnected...");
        animateRingToColor(COLOR_R_OFF, COLOR_G_OFF, COLOR_B_OFF, false);
        return; 
    }

    // --- EMPTY STATE ---
    if (!radar.stationaryTargetDetected() && !radar.movingTargetDetected())
    {
      if (totalEnergy > 0) {
        for (int i = 0; i < HISTORY_LENGTH; i++) { energyHistory[i] = 0; }
        totalEnergy = 0;
        historyIndex = 0;
      }
      
      focusStartTime = 0;
      focusLevel = 0;
      breakNeeded = false;
      
      animateRingToColor(COLOR_R_OFF, COLOR_G_OFF, COLOR_B_OFF, true);
      // Removed print to avoid serial spam when empty, or uncomment below:
      // MONITOR_SERIAL.println("Empty: No target."); 
      return; 
    }

    // Check for moving targets OUTSIDE your bubble
    if (radar.movingTargetDetected())
    {
      int dist = radar.movingTargetDistance();
      
      if (dist > FOCUS_AREA_DISTANCE)
      {
        focusStartTime = 0; 
        
        if (dist > ZONE_YELLOW && dist <= ZONE_BLUE)
        {
          animateRingToColor(COLOR_R_BLUE, COLOR_G_BLUE, COLOR_B_BLUE, true);
          MONITOR_SERIAL.print(F("PROXIMITY (BLUE) - Dist: ")); MONITOR_SERIAL.println(dist);
          return;
        }
        else if (dist > ZONE_RED && dist <= ZONE_YELLOW)
        {
          animateRingToColor(COLOR_R_YELLOW, COLOR_G_YELLOW, COLOR_B_YELLOW, true);
          MONITOR_SERIAL.print(F("PROXIMITY (YELLOW) - Dist: ")); MONITOR_SERIAL.println(dist);
          return;
        }
        else if (dist > FOCUS_AREA_DISTANCE && dist <= ZONE_RED)
        {
          animateRingToColor(COLOR_R_RED, COLOR_G_RED, COLOR_B_RED, true);
          MONITOR_SERIAL.print(F("PROXIMITY (RED) - Dist: ")); MONITOR_SERIAL.println(dist);
          return;
        }
      }
    }

    // --- LOGIC 2: BREAK REMINDER (Priority 2) ---
    if (breakNeeded) {
      animateRingToColor(COLOR_R_PURPLE, COLOR_G_PURPLE, COLOR_B_PURPLE, true);
      MONITOR_SERIAL.println("🚨 TAKE A BREAK!");
      return; 
    }

    // --- LOGIC 3: FOCUS/DISTRACTION (Default) ---
    int currentEnergy = 0;
    
    // Only count energy if movement is INSIDE the bubble
    if (radar.movingTargetDetected() && radar.movingTargetDistance() <= FOCUS_AREA_DISTANCE)
    {
      currentEnergy = radar.movingTargetEnergy();
    }
    
    // Update Moving Average
    totalEnergy = totalEnergy - energyHistory[historyIndex];
    energyHistory[historyIndex] = currentEnergy;
    totalEnergy = totalEnergy + currentEnergy;
    historyIndex = (historyIndex + 1) % HISTORY_LENGTH;
    int averageEnergy = totalEnergy / HISTORY_LENGTH;

    if (averageEnergy > DISTRACTION_ENERGY_THRESHOLD)
    {
      // --- DISTRACTION ---
      animateRingToColor(COLOR_R_RED, COLOR_G_RED, COLOR_B_RED, true);
      MONITOR_SERIAL.print("DISTRACTION! Avg Energy: ");
      MONITOR_SERIAL.println(averageEnergy);
      
      focusStartTime = 0;
      focusLevel = 0;
      lastStateChange = millis();
    }
    else
    {
      // --- FOCUS ---
      if (focusStartTime == 0) {
        focusStartTime = millis(); 
      }
      
      if ((millis() - focusStartTime) >= FOCUS_TIME_LIMIT_MS) {
        breakNeeded = true;
        lastStateChange = millis();
        return;
      }
      
      focusLevel = focusLevel + BRIGHTNESS_STEP;
      focusLevel = min(focusLevel, LED_BRIGHTNESS_MAX - LED_BRIGHTNESS_BASE); 
      
      uint8_t scaledBrightness = LED_BRIGHTNESS_BASE + focusLevel;
      
      animateRingToColor(COLOR_R_GREEN, COLOR_G_GREEN, COLOR_B_GREEN, true, scaledBrightness);
      
      MONITOR_SERIAL.print("FOCUSED. Energy: ");
      MONITOR_SERIAL.print(averageEnergy);
      MONITOR_SERIAL.print(" | Time: "); 
      MONITOR_SERIAL.print((millis() - focusStartTime) / 1000L); 
      MONITOR_SERIAL.println("s");
    }
  }
}

// --- Helper Functions ---

void setRingColorImmediate(uint32_t color)
{
  for (int i = 0; i < ring.numPixels(); i++) {
    ring.setPixelColor(i, color);
  }
  ring.show();
  currentColor = color;
}

void animateRingToColor(uint8_t targetR, uint8_t targetG, uint8_t targetB, bool isFading, uint8_t brightness)
{
  uint32_t newTargetColor = ring.Color(targetR, targetG, targetB);

  // If color matches, just update brightness
  if (newTargetColor == (currentColor & 0xFFFFFF)) {
      if(ring.getBrightness() != brightness) {
        ring.setBrightness(brightness);
        ring.show();
      }
      return;
  }

  if (isFading) {
    uint8_t currentR = (currentColor >> 16) & 0xFF;
    uint8_t currentG = (currentColor >> 8) & 0xFF;
    uint8_t currentB = currentColor & 0xFF;

    int stepR = (targetR - currentR) / 5; // Faster steps
    int stepG = (targetG - currentG) / 5;
    int stepB = (targetB - currentB) / 5;
    
    currentR += stepR;
    currentG += stepG;
    currentB += stepB;

    // Snap to target if close
    if (abs(currentR - targetR) < 10) currentR = targetR;
    if (abs(currentG - targetG) < 10) currentG = targetG;
    if (abs(currentB - targetB) < 10) currentB = targetB;
    
    uint32_t intermediateColor = ring.Color(currentR, currentG, currentB);
    ring.setBrightness(brightness); 
    setRingColorImmediate(intermediateColor); 
  } else {
    ring.setBrightness(brightness);
    setRingColorImmediate(newTargetColor);
  }
}