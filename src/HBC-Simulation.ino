/*
 * TC Simulator Firmware (Simplified)
 * Description: Emits thermocouple readings as JSON over Serial1:
 *              {"h":"tc","d":[0,70.00,0,1,70.00,0,2,70.00,0,...]}$
 *              Flat array format: triplets of (index, temperature, faultCode)
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
#define TC_SEND_INTERVAL_MS 1000  // Send TC data every 1 second (matches interface update rate)

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
  
  // Async communication - Process incoming messages (power)
  if(serial1CommsManager.Update()){
    const char* messageType = serial1CommsManager.GetMessageTypeCStr();
    
    // Route messages based on header type
    if(messageType && strcmp(messageType, "power") == 0){
      // JSON power data format: {"h":"power","p":[val1,val2,...]}
      std::vector<ChannelPowerData> channelPowers = serial1CommsManager.ParsePowerDataJson(16);
      
      // Display received power values
      Serial.print("RECV POWER: [");
      for(unsigned int i = 0; i < channelPowers.size() && i < NUM_CHANNELS; i++){
        if(i > 0) Serial.print(",");
        Serial.print(channelPowers[i].powerPercent, 1);
      }
      Serial.println("]");
      
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
    }
  }
  
  // Send TC data periodically
  if((now - lastTcSend) >= TC_SEND_INTERVAL_MS){
    lastTcSend = now;
    
    // Format and send all TCs in one message (flat array format)
    // Format: {"h":"tc","d":[0,70.00,0,1,70.00,0,2,70.00,0,...]}$
    // Triplets: index, temperature, faultCode
    // Use StaticJsonDocument with static storage to avoid heap allocation
    static StaticJsonDocument<2048> tcDoc; // Large enough for 50 TCs (150 values)
    tcDoc.clear();
    tcDoc["h"] = "tc";
    JsonArray tcDataArray = tcDoc.createNestedArray("d");
    
    for(int i = 0; i < NUM_TCS; i++){
      tcDataArray.add(i);                    // TC index
      double tcTemp = round2(simulatedTCValues[i]);
      tcDataArray.add(tcTemp);               // Temperature
      tcDataArray.add(tcFaultCodes[i]);      // Fault code
    }
    
    // Serialize to static buffer and send - no heap allocation
    static char tcDataBuffer[2560];  // Buffer for TC JSON (2048 + overhead)
    serializeJson(tcDoc, tcDataBuffer, sizeof(tcDataBuffer));
    // Append '$' delimiter
    size_t len = strlen(tcDataBuffer);
    if(len < sizeof(tcDataBuffer) - 2){
      tcDataBuffer[len] = '$';
      tcDataBuffer[len + 1] = '\0';
    }
    
    // Display raw message being sent
    Serial.print("SEND TC: ");
    Serial.println(tcDataBuffer);
    
    serial1CommsManager.WriteSerialMessage(tcDataBuffer);
  }
  
  Particle.process();
}

// Helper function to round to 2 decimal places (matching main firmware)
double round2(double value) {
  return (int)(value * 100 + 0.5) / 100.0;
}
