/*
 * TC Simulator Firmware (Simplified)
 * Description: Emits thermocouple readings as JSON over Serial1 using the same
 *              message format as the main firmware expects:
 *              {"h":"tc","d":[idx,temp,fault, idx,temp,fault, ...]}$
 */

SYSTEM_THREAD(ENABLED);

// Define connection options
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#include <ArduinoJson.h>
#include "Serial1CommsManager.h"

// Serial1CommsManager instance
Serial1CommsManager serial1CommsManager(Serial1);

// Serial1 configuration (RX/TX pins on Particle Argon)
#define SERIAL1_BAUD 115200

// Update intervals
#define UPDATE_INTERVAL 1000  // Thermal simulation update (1 second)
#define TC_SEND_INTERVAL_MS 500  // Send TC data every 0.5 seconds (ensures fresh data for 1-second interface updates)

// Initial simulated TC values
// Set them all to 70.00 Deg F
#define NUM_TCS 50
double simulatedTCValues[NUM_TCS];

// Power percentage values received from COTS firmware (channels 0-15)
// Updated when power messages are received over Serial1
#define NUM_CHANNELS 16
double channelPowerPercent[NUM_CHANNELS];

// Thermal simulation constants
#define HEAT_CAP 1000.0        // Heat capacity (W*sec/°C)
#define HEAT_TRANSFER_HA 50.0  // Heat transfer coefficient (W/°C)
#define AMBIENT_TEMP 70.0      // Ambient temperature (°F)
#define MAX_POWER 10000.0       // Maximum power (W) - used to convert percentage to actual power

unsigned long lastUpdate = 0;
unsigned long lastTcSend = 0;
int tcFaultCodes[NUM_TCS];  // TC fault codes array

void setup() {
  // Initialize Serial1 for communication with main Particle device
  Serial1.begin(SERIAL1_BAUD);
  
  // Initialize Serial (USB) for debugging (optional)
  Serial.begin(115200);
  
  // Wait for serial connection (optional, for debugging)
  // while (!Serial && millis() < 5000) {
  //   Particle.process();
  // }
  
  // Initialize all TC values to 70.0°F
  for (int i = 0; i < NUM_TCS; i++) {
    simulatedTCValues[i] = 70.0;
  }
  
  // Initialize all channel power percentages to 0.0
  for (int i = 0; i < NUM_CHANNELS; i++) {
    channelPowerPercent[i] = 0.0;
  }
  
  // Initialize all TC fault codes to 0 (no fault)
  for (int i = 0; i < NUM_TCS; i++) {
    tcFaultCodes[i] = 0;
  }
  
  // Initialize Serial1CommsManager (Serial1.begin() already called above)
  serial1CommsManager.Begin(SERIAL1_BAUD);  // Begin() is now empty, kept for compatibility
  
  delay(1000); // Give Serial1 time to initialize
  
  Serial.println("TC Simulator Started (Async Mode)");
  Serial.print("Sending TC data for indices 0-");
  Serial.println(NUM_TCS - 1);
  Serial.print("Receiving power data for ");
  Serial.print(NUM_CHANNELS);
  Serial.println(" channels from main firmware over Serial1");
}

void loop() {
  unsigned long now = millis();
  
  // Update thermal simulation every second
  if ((now - lastUpdate) >= UPDATE_INTERVAL) {
    lastUpdate = now;
    
    // Simulate temperature values based on channel power percentages
    // Each channel has exactly 3 TCs: 
    // Channel 0: TCs 0-2, Channel 1: TCs 3-5, ..., Channel 15: TCs 45-47
    // TCs 48-49 are not used (only 48 TCs used out of 50)
    for (int i = 0; i < NUM_TCS; i++) {
      int channelIndex = i / 3;  // 3 TCs per channel
      
      // Only simulate TCs 0-47 (48 TCs total, leaving out TCs 48-49)
      if (channelIndex >= NUM_CHANNELS) {
        // TCs 48-49: don't apply power, just maintain ambient temperature
        channelIndex = -1;  // Mark as unused
      }
      
      double powerPercent = (channelIndex >= 0) ? channelPowerPercent[channelIndex] : 0.0;
      
      // Convert power percentage to actual power (W)
      double Powerin = (powerPercent / 100.0) * MAX_POWER;
      
      // Get current temperature
      double Temp = simulatedTCValues[i];
      
      // Calculate temperature derivatives
      double Tdotin = Powerin / HEAT_CAP;                              // W / (W*sec/°C) = °C/sec
      double Tdotout = (Temp - AMBIENT_TEMP) * HEAT_TRANSFER_HA / HEAT_CAP;  // W / (W*sec/°C) = °C/sec
      double Tdot = Tdotin - Tdotout;                                  // °C/sec
      
      // Update temperature
      simulatedTCValues[i] = Temp + Tdot * UPDATE_INTERVAL / 1000.0;   // °F
    }
  }
  
  // Async communication - Process incoming messages (power or memory)
  if(serial1CommsManager.Update()){
    String messageType = serial1CommsManager.GetMessageType();
    
    // Route messages based on header type
    if(messageType == "power"){
      // JSON power data format: {"h":"power","p":[val1,val2,...]}
      std::vector<ChannelPowerData> channelPowers = serial1CommsManager.ParsePowerDataJson(16);
      
      // Update power values in our array
      for(unsigned int i = 0; i < channelPowers.size(); i++){
        int chIndex = channelPowers[i].channelIndex;
        if(chIndex >= 0 && chIndex < NUM_CHANNELS){
          channelPowerPercent[chIndex] = channelPowers[i].powerPercent;
          
          // Clamp to 0-100%
          if(channelPowerPercent[chIndex] < 0.0) channelPowerPercent[chIndex] = 0.0;
          if(channelPowerPercent[chIndex] > 100.0) channelPowerPercent[chIndex] = 100.0;
        }
      }
    } else if(messageType == "memory"){
      // JSON memory statistics format: {"h":"memory","mT":...,"mL":...,"mF":...}
      MemoryStats memStats;
      if(serial1CommsManager.ParseMemoryStatsJson(memStats)){
        // Memory stats received - can be logged or forwarded to interface
        // For now, just print to Serial for debugging
        Serial.print("Memory Stats - Total: ");
        Serial.print(memStats.totalFree);
        Serial.print(" bytes, Largest: ");
        Serial.print(memStats.largestBlock);
        Serial.print(" bytes, Fragmentation: ");
        Serial.print(memStats.fragmentation);
        Serial.println("%");
      }
    }
  }
  
  // Send TC data periodically
  if((now - lastTcSend) >= TC_SEND_INTERVAL_MS){
    lastTcSend = now;
    
    // Format and send TC data as JSON with header (uniform with power and memory messages)
    // Format: {"h":"tc","d":[0,70.00,0,1,70.00,0,...]}
    StaticJsonDocument<3072> tcDoc; // Large enough for 50 TCs (50 * 3 * ~20 bytes)
    tcDoc["h"] = "tc";
    JsonArray tcDataArray = tcDoc.createNestedArray("d");
    
    for(int i = 0; i < NUM_TCS; i++){
      tcDataArray.add(i);                    // TC index
      double tcTemp = round2(simulatedTCValues[i]);
      tcDataArray.add(tcTemp); // Temperature
      tcDataArray.add(tcFaultCodes[i]);      // Fault code
    }
    
    String tcDataString;
    serializeJson(tcDoc, tcDataString);
    serial1CommsManager.WriteSerialMessage(tcDataString + "$");
  }
  
  Particle.process();
}

// Helper function to round to 2 decimal places (matching main firmware)
double round2(double value) {
  return (int)(value * 100 + 0.5) / 100.0;
}
