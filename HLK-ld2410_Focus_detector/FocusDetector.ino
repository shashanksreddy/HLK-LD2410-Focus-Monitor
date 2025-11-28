/*
 * HLK-LD2410 & ESP32 mmWave Radar "Focus Detector"
 * Based on the concept: "Deep Work Guardian"
 * * Logic:
 * 1. User Zone (0-1.5m): Ignored via App configuration (Gates 0-1 set to sens 0).
 * 2. Distraction Zone (1.5m-3m): Monitored.
 * 3. Break Timer: Alerts after 52 minutes.
 */

/ --- Libraries ---
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
#define LED_BRIGHTNESS_MAX 255 // Max raw brightness (used for scaling)
#define LED_BRIGHTNESS_BASE 50 // Base brightness for all colors (and min for Green)


// --- Focus Logic Settings (Your Bubble) ---
#define DISTRACTION_ENERGY_THRESHOLD 30 // Threshold for the AVERAGE energy
#define CHECK_DELAY 250                 // 4 times per second (Tuning the fade speed)
#define HISTORY_LENGTH 10               // Store 10 values


// --- Proximity Alert Zone Settings ---a
#define FOCUS_AREA_DISTANCE 100         // Your 1-meter bubble
#define ZONE_RED 150                    // 1.5 meters
#define ZONE_YELLOW 200                 // 2.0 meters
#define ZONE_BLUE 300                   // 3.0 meters


// --- Break Reminder Settings (The 52/17 Rule) ---
#define FOCUS_TIME_LIMIT_MS (52L * 60L * 1000L) // 52 minutes in milliseconds
#define BREAK_TIME_MS (17L * 60L * 1000L)      // 17 minutes in milliseconds
#define BRIGHTNESS_STEP 20 // How much to increase brightness per check (20 * 4 checks/sec = 80 per second)


// --- Color Definitions (R, G, B) - Now defined by their components ---
// Note: Actual color is calculated in setRingColor to include brightness.
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


#define COLOR_R_PURPLE 128 // For Break Reminder
#define COLOR_G_PURPLE 0
#define COLOR_B_PURPLE 128


#define COLOR_R_OFF 0
#define COLOR_G_OFF 0
#define COLOR_B_OFF 0


// --- Global Variables for Moving Average ---
int energyHistory[HISTORY_LENGTH];
long totalEnergy = 0;
int historyIndex = 0;


// --- Global Variables for Fading and Brightness ---
uint32_t currentColor = 0; // Stores the current color for fading
uint32_t targetColor = 0; // Stores the target color for fading
int focusLevel = 0;       // Tracks continuous focus time for brightness scaling (0 to LED_BRIGHTNESS_MAX)


// --- Global Variables for Break Reminder ---
unsigned long focusStartTime = 0;
bool breakNeeded = false;
unsigned long lastStateChange = 0; // Used to debounce the break timer


// --- Objects ---
ld2410 radar;
Adafruit_NeoPixel ring = Adafruit_NeoPixel(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);


// --- Function Prototypes ---
void animateRingToColor(uint8_t targetR, uint8_t targetG, uint8_t targetB, bool isFading, uint8_t brightness = LED_BRIGHTNESS_BASE);
void setRingColorImmediate(uint32_t color);


void setup(void)
{
 MONITOR_SERIAL.begin(115200);
 // Setting a lower baud rate for LD2410 can sometimes be more stable, though 256000 is default.
 RADAR_SERIAL.begin(256000, SERIAL_8N1, RADAR_RX_PIN, RADAR_TX_PIN);


 ring.begin();
 ring.setBrightness(LED_BRIGHTNESS_BASE); // Set a moderate base brightness
 ring.show();


 for (int i = 0; i < HISTORY_LENGTH; i++) {
   energyHistory[i] = 0;
 }
 MONITOR_SERIAL.println(F("History array cleared."));
 delay(500);


 MONITOR_SERIAL.println(F("\nRadar Focus/Proximity Project: Initializing..."));


 if (radar.begin(RADAR_SERIAL))
 {
   MONITOR_SERIAL.println(F("Radar sensor connected."));


   // Set max range to Gate 4 (approx 3 meters)
   if (radar.setMaxValues(4, 4, 0)) {
     MONITOR_SERIAL.println(F("Sensor range set to 3m (Gate 4) OK."));
   } else {
     MONITOR_SERIAL.println(F("Failed to set sensor range."));
   }
 }
 else
 {
   MONITOR_SERIAL.println(F("Radar sensor not connected!"));
 }
  // Initialize the color
 currentColor = ring.Color(COLOR_R_OFF, COLOR_G_OFF, COLOR_B_OFF);
 targetColor = currentColor;
}


void loop()
{
 delay(CHECK_DELAY);
  // Check if enough time has passed to reset the break timer
 if (breakNeeded && (millis() - lastStateChange) > BREAK_TIME_MS) {
     breakNeeded = false;
     focusStartTime = millis();
     MONITOR_SERIAL.println(F("Break time over. Focus timer restarted."));
 }


 // --- PROXIMITY ALERT (Priority 1) ---
 if (radar.read()) {
   if (!radar.isConnected()) {
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
    
     // If there is no one, reset focus state and turn off
     focusStartTime = 0;
     focusLevel = 0;
     breakNeeded = false;
    
     animateRingToColor(COLOR_R_OFF, COLOR_G_OFF, COLOR_B_OFF, true);
     MONITOR_SERIAL.println("Empty: No target. History cleared.");
     return;
   }


   // Check for moving targets OUTSIDE your bubble first
   if (radar.movingTargetDetected())
   {
     int dist = radar.movingTargetDistance();
    
     // Proximity alerts override focus/break logic
     if (dist > FOCUS_AREA_DISTANCE)
     {
       focusStartTime = 0; // Reset focus timer on external distraction
      
       if (dist > ZONE_YELLOW && dist <= ZONE_BLUE)
       {
         animateRingToColor(COLOR_R_BLUE, COLOR_G_BLUE, COLOR_B_BLUE, true);
         MONITOR_SERIAL.println(String("PROXIMITY ALERT (BLUE) - Dist: ")+ dist + "cm");


         return;
       }
       else if (dist > ZONE_RED && dist <= ZONE_YELLOW)
       {
         animateRingToColor(COLOR_R_YELLOW, COLOR_G_YELLOW, COLOR_B_YELLOW, true);
         MONITOR_SERIAL.println(String("PROXIMITY ALERT (YELLOW) - Dist: ") + dist + " cm");
         return;
       }
       else if (dist > FOCUS_AREA_DISTANCE && dist <= ZONE_RED)
       {
         animateRingToColor(COLOR_R_RED, COLOR_G_RED, COLOR_B_RED, true);
         MONITOR_SERIAL.println(String ("PROXIMITY ALERT (RED) - Dist: ")+ dist + "cm");
         return;
       }
     }
   }
 }




 // --- LOGIC 2: BREAK REMINDER (Priority 2) ---
 if (breakNeeded) {
   animateRingToColor(COLOR_R_PURPLE, COLOR_G_PURPLE, COLOR_B_PURPLE, true);
   MONITOR_SERIAL.println("🚨 TAKE A BREAK! Focus time exceeded 52 minutes. Purple Alert.");
   return; // Break alert overrides focus/distraction logic
 }
  // --- LOGIC 3: FOCUS/DISTRACTION (Default) ---
  int currentEnergy = 0;
  // Movement is inside the 1m bubble
 if (radar.movingTargetDetected() && radar.movingTargetDistance() <= FOCUS_AREA_DISTANCE)
 {
   currentEnergy = radar.movingTargetEnergy();
 }
 // You are just sitting still (currentEnergy remains 0)


 // --- Update the Moving Average ---
 totalEnergy = totalEnergy - energyHistory[historyIndex];
 energyHistory[historyIndex] = currentEnergy;
 totalEnergy = totalEnergy + currentEnergy;
 historyIndex = (historyIndex + 1) % HISTORY_LENGTH;
 int averageEnergy = totalEnergy / HISTORY_LENGTH;


 // --- Set the LED based on the AVERAGE energy ---
 if (averageEnergy > DISTRACTION_ENERGY_THRESHOLD)
 {
   // --- DISTRACTION ---
   animateRingToColor(COLOR_R_RED, COLOR_G_RED, COLOR_B_RED, true);
   MONITOR_SERIAL.print("Focus Bubble: DISTRACTION! Avg Energy: ");
   MONITOR_SERIAL.println(averageEnergy);
  
   // Reset focus timer and level
   focusStartTime = 0;
   focusLevel = 0;
   lastStateChange = millis();


 }
 else
 {
   // --- FOCUS ---
   if (focusStartTime == 0) {
     focusStartTime = millis(); // Start the timer
   }
  
   // Check if break is needed
   if ((millis() - focusStartTime) >= FOCUS_TIME_LIMIT_MS) {
     breakNeeded = true;
     lastStateChange = millis();
     // On the next loop iteration, the break logic (Priority 2) will run.
     MONITOR_SERIAL.print("Focus time exceeded! Setting breakNeeded=true.");
     return;
   }
  
   // Increase focus brightness level
   focusLevel = focusLevel + BRIGHTNESS_STEP;
   focusLevel = min(focusLevel, LED_BRIGHTNESS_MAX - LED_BRIGHTNESS_BASE); // Cap the extra brightness
  
   // Scale the base brightness based on focus time
   uint8_t scaledBrightness = LED_BRIGHTNESS_BASE + focusLevel;
  
   // Use the scaled brightness for the GREEN color
   animateRingToColor(COLOR_R_GREEN, COLOR_G_GREEN, COLOR_B_GREEN, true, scaledBrightness);
  
   MONITOR_SERIAL.print("Focus Bubble: FOCUSED. Avg Energy: ");
   MONITOR_SERIAL.print(" Focus Time: "); MONITOR_SERIAL.print((millis() - focusStartTime) / 1000L); MONITOR_SERIAL.println("s");
  
   // If the timer is about to run out, flash a warning
   if ((millis() - focusStartTime) > (FOCUS_TIME_LIMIT_MS - 30000L)) { // 30 seconds warning
       // Implement a subtle warning like a brief yellow flash if you want,
       // but for now, the brightness scaling is the primary feedback.
   }
 }
}


// --- Helper function to set all pixels to a color immediately ---
void setRingColorImmediate(uint32_t color)
{
 for (int i = 0; i < ring.numPixels(); i++) {
   ring.setPixelColor(i, color);
 }
 ring.show();
 currentColor = color; // Update current color
}


// --- Helper function to smoothly transition the color ---
// Optional: The 'brightness' argument is for focus scaling, only used with GREEN.
void animateRingToColor(uint8_t targetR, uint8_t targetG, uint8_t targetB, bool isFading, uint8_t brightness) // <-- FIXED
{
 uint32_t newTargetColor = ring.Color(targetR, targetG, targetB);


 // If the new target is the same as the current color, just apply brightness and exit
 if (newTargetColor == (currentColor & 0xFFFFFF)) {
     ring.setBrightness(brightness);
     ring.show();
     return;
 }
  if (isFading) {
   // Extract current color components
   uint8_t currentR = (currentColor >> 16) & 0xFF;
   uint8_t currentG = (currentColor >> 8) & 0xFF;
   uint8_t currentB = currentColor & 0xFF;


   // Calculate step size for 10 steps
   int stepR = (targetR - currentR) / 10;
   int stepG = (targetG - currentG) / 10;
   int stepB = (targetB - currentB) / 10;
  
   // For fast transition, only do one step towards the target
   currentR = currentR + stepR;
   currentG = currentG + stepG;
   currentB = currentB + stepB;


   // Ensure we don't overshoot the final color in the final step
   if ((stepR > 0 && currentR > targetR) || (stepR < 0 && currentR < targetR)) currentR = targetR;
   if ((stepG > 0 && currentG > targetG) || (stepG < 0 && currentG < targetG)) currentG = targetG;
   if ((stepB > 0 && currentB > targetB) || (stepB < 0 && currentB < targetB)) currentB = targetB;
  
   // Set the ring to the intermediate color
   uint32_t intermediateColor = ring.Color(currentR, currentG, currentB);
   ring.setBrightness(brightness); // Apply scaled brightness
   setRingColorImmediate(intermediateColor); // Update the ring and currentColor
 } else {
   // No fading, set immediately
   ring.setBrightness(brightness);
   setRingColorImmediate(newTargetColor);
 }
}
