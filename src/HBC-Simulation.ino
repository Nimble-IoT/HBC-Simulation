/*
 * TC Simulator Firmware
 * Description: Simulates 50 thermocouples (TCs 0-49) and sends data over Serial1
 * Author: Auto-generated for HBC-COTS-Firmware test bench
 * Date: 2024
 */

SYSTEM_THREAD(ENABLED);

// Define connection options
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#include <ArduinoJson.h>
#include <string.h>
#include "Serial1CommsManager.h"

// TC Data Structure - holds all values for a single thermocouple
struct TCData {
  int index;           // TC index (0-49)
  double temperature;  // TC reading in degrees
  int faultCode;       // TC error/fault code (0 = no error)
};

// Serial1CommsManager instance
Serial1CommsManager serial1CommsManager(Serial1);

// Serial1 configuration (RX/TX pins on Particle Argon)
#define SERIAL1_BAUD 115200

// Update intervals
#define UPDATE_INTERVAL 500  // Thermal simulation update (500 ms )
#define TC_SEND_INTERVAL_MS 500  // Send TC data every 500ms

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

// Fault simulation mode
enum FaultSimulationMode {
  MODE_NORMAL = 0,    // Use power values from Serial1 as normal
  MODE_FAULT_ZERO = 1,  // Override selected channels to 0%
  MODE_FAULT_FULL = 2   // Override selected channels to 100%
};

FaultSimulationMode faultMode = MODE_NORMAL;
bool faultChannelMask[NUM_CHANNELS];  // true = channel is in fault mode, false = normal

// Forward declarations for Particle cloud functions
int setFaultSimulation(String command);  // Format: "CH,Mode" where CH is channels and Mode is normal/zero/max
int getFaultStatus(String command);

// Forward declaration
String FormatTCDataForSend(double tcValues[], int tcFaultCodes[], int numTCs, int valuesPerTC = 3);

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
  
  // Initialize fault channel mask (all channels start in normal mode)
  for (int i = 0; i < NUM_CHANNELS; i++) {
    faultChannelMask[i] = false;
  }
  faultMode = MODE_NORMAL;
  
  // Register Particle cloud functions
  Particle.function("setFaultSimulation", setFaultSimulation);
  Particle.function("getFaultStatus", getFaultStatus);
  
  // Initialize Serial1CommsManager (Serial1.begin() already called above)
  serial1CommsManager.Begin(SERIAL1_BAUD);  // Begin() is now empty, kept for compatibility
  
  delay(1000); // Give Serial1 time to initialize
  
  Serial.println("TC Simulator Started (Async Mode)");
  Serial.print("Sending TC data for indices 0-");
  Serial.println(NUM_TCS - 1);
  Serial.print("Receiving power data for ");
  Serial.print(NUM_CHANNELS);
  Serial.println(" channels from COTS firmware over Serial1");
  Serial.println("Fault simulation mode: NORMAL");
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
      
      // Apply fault simulation overrides if channel is in fault mode
      if (channelIndex >= 0 && channelIndex < NUM_CHANNELS && faultChannelMask[channelIndex]) {
        if (faultMode == MODE_FAULT_ZERO) {
          powerPercent = 0.0;
        } else if (faultMode == MODE_FAULT_FULL) {
          powerPercent = 100.0;
        }
        // MODE_NORMAL: use original powerPercent (no override)
      }
      
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
  
  // Async communication - Process incoming power data
  // Check if data is available but not yet processed (for debugging)
  if(Serial1.available() > 0){
    Serial.print("[DEBUG] Serial1.available() = ");
    Serial.println(Serial1.available());
  }
  
  if(serial1CommsManager.Update()){
    // Display raw message received for debugging
    String rawMsg = serial1CommsManager.GetLastRawMessage();
    Serial.print("[RAW RX] ");
    Serial.println(rawMsg);
    
    std::vector<ChannelPowerData> channelPowers = serial1CommsManager.ParseChannelPowerData(16);
    
    // Display parsed data for debugging
    Serial.print("[PARSED] Channels: ");
    Serial.print(channelPowers.size());
    Serial.print(" | Values: ");
    for(unsigned int i = 0; i < channelPowers.size() && i < 5; i++){  // Show first 5
      Serial.print("Ch");
      Serial.print(channelPowers[i].channelIndex);
      Serial.print("=");
      Serial.print(channelPowers[i].powerPercent, 2);
      if(i < channelPowers.size() - 1 && i < 4) Serial.print(", ");
    }
    if(channelPowers.size() > 5) Serial.print("...");
    Serial.println();
    
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
  
  // Send TC data periodically (every 1 second)
  if((now - lastTcSend) >= TC_SEND_INTERVAL_MS){
    lastTcSend = now;
    
    // Format and send TC data
    String tcDataString = FormatTCDataForSend(simulatedTCValues, tcFaultCodes, NUM_TCS, 3);
    serial1CommsManager.WriteSerialMessage(tcDataString + "$");
  }
  
  Particle.process();
}

// Helper function to round to 2 decimal places (matching main firmware)
double round2(double value) {
  return (int)(value * 100 + 0.5) / 100.0;
}

// Format TC data for sending back to firmware
// Takes TC values and returns a comma-separated string ready to send with $
// Format: index0,temp0,fault0,index1,temp1,fault1,...
String FormatTCDataForSend(double tcValues[], int tcFaultCodes[], int numTCs, int valuesPerTC) {
  // MEMORY FIX: Pre-allocate string capacity to avoid multiple reallocations
  // Estimate: ~10 chars per TC (index, temp, fault + commas) * numTCs
  int estimatedLength = numTCs * 12; // 12 chars per TC with some margin
  String output = "";
  output.reserve(estimatedLength);
  
  for (int i = 0; i < numTCs; i++) {
    if (i > 0) output += ",";
    
    output += String(i);           // TC index
    output += ",";
    output += String(round2(tcValues[i]), 2); // Temperature (2 decimal places)
    output += ",";
    output += String(tcFaultCodes[i]);        // Fault code
  }
  
  return output;
}

// Set fault simulation for a single channel
// Command format: "CH,Mode" where:
//   CH = single channel number (0-15)
//   Mode = "normal", "zero", or "max"
// Examples:
//   "0,zero"      - Channel 0 forced to 0%
//   "5,max"       - Channel 5 forced to 100%
//   "3,normal"    - Channel 3 uses normal power from Serial1
// Returns: 0 on success, -1 on error
int setFaultSimulation(String command) {
  command.trim();
  command.toLowerCase();
  
  int commaIdx = command.indexOf(',');
  if (commaIdx == -1) {
    return -1;
  }
  
  String channelStr = command.substring(0, commaIdx);
  String modeSpec = command.substring(commaIdx + 1);
  channelStr.trim();
  modeSpec.trim();
  
  // Parse channel number
  int channelNum = channelStr.toInt();
  if (channelNum < 0 || channelNum >= NUM_CHANNELS) {
    return -1;
  }
  
  // Parse and set the mode
  if (modeSpec == "normal") {
    faultMode = MODE_NORMAL;
    faultChannelMask[channelNum] = false;
  } else if (modeSpec == "zero") {
    faultMode = MODE_FAULT_ZERO;
    faultChannelMask[channelNum] = true;
  } else if (modeSpec == "max") {
    faultMode = MODE_FAULT_FULL;
    faultChannelMask[channelNum] = true;
  } else {
    return -1;
  }
  
  return 0;
}

// Get current fault simulation status
// Returns: Status string with mode and affected channels
int getFaultStatus(String command) {
  String modeStr;
  switch (faultMode) {
    case MODE_NORMAL:
      modeStr = "NORMAL";
      break;
    case MODE_FAULT_ZERO:
      modeStr = "FAULT_ZERO";
      break;
    case MODE_FAULT_FULL:
      modeStr = "FAULT_MAX";
      break;
    default:
      modeStr = "UNKNOWN";
      break;
  }
  
  // Build list of affected channels
  String channelList = "";
  int count = 0;
  for (int i = 0; i < NUM_CHANNELS; i++) {
    if (faultChannelMask[i]) {
      if (count > 0) channelList += ",";
      channelList += String(i);
      count++;
    }
  }
  if (count == 0) {
    channelList = "none";
  }
  return 0;
}

