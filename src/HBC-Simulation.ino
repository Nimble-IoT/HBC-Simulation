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

// Serial1 configuration (RX/TX pins on Particle Argon)
#define SERIAL1_BAUD 115200

// Update interval (1 second = 1000ms)
#define UPDATE_INTERVAL 1000

// Power data received from COTS firmware over Serial1
// Power percentages are sent every 250ms from COTS firmware

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
String serial1Buffer = ""; // Buffer for accumulating Serial1 data

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
  
  delay(1000); // Give Serial1 time to initialize
  
  Serial.println("TC Simulator Started");
  Serial.print("Sending TC data for indices 0-");
  Serial.println(NUM_TCS - 1);
  Serial.print("Receiving power data for ");
  Serial.print(NUM_CHANNELS);
  Serial.println(" channels from COTS firmware over Serial1");
}

void loop() {
  unsigned long now = millis();
  
  // Process incoming power data from COTS firmware over Serial1
  processPowerData();
  
  // Update every second
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
    
    // Create JSON document for all 50 TCs
    DynamicJsonDocument tcDataDoc(6144);  // Increased size for 50 TCs (50 * ~100 bytes + overhead)
    tcDataDoc["h"] = "tcSim"; // Header: thermocouple simulator
    
    // Create array of TC data
    JsonArray tcArray = tcDataDoc.createNestedArray("tcs");
    
    for (int i = 0; i < NUM_TCS; i++) {
      JsonObject tcObj = tcArray.createNestedObject();
      tcObj["i"] = i;                    // Index
      tcObj["t"] = round2(simulatedTCValues[i]); // Temperature
      tcObj["fC"] = 0;                   // Fault code (0 = no fault)
    }
    
    // Serialize and send over Serial1
    String tcDataString;
    serializeJson(tcDataDoc, tcDataString);
    
    // Check if serialization was successful (document not too small)
    if (tcDataDoc.overflowed()) {
      Serial.println("ERROR: TC data JSON document overflowed! Increase size.");
    }
    
    // TODO: REMOVE DEBUG - Log sending info periodically
    static unsigned long lastSendDebug = 0;
    static int sendCount = 0;
    if ((now - lastSendDebug) >= 10000) {  // Every 10 seconds
      lastSendDebug = now;
      Serial.print("[Sim] Sent TC data #");
      Serial.print(++sendCount);
      Serial.print(", length: ");
      Serial.print(tcDataString.length());
      Serial.print(" bytes, TCs: ");
      Serial.print(NUM_TCS);
      Serial.print(", time: ");
      Serial.println(now);
      
      // Show first 200 chars
      int showLen = min(200, (int)tcDataString.length());
      Serial.print("[Sim] Message preview (first ");
      Serial.print(showLen);
      Serial.print(" chars): ");
      Serial.println(tcDataString.substring(0, showLen));
    }
    
    // Send with explicit newline
    Serial1.print(tcDataString);
    Serial1.print('\n');  // Explicit newline to ensure message delimiter
    
    // Optional: Also print to Serial for debugging
    // Serial.println("Sent: " + tcDataString);
    // TODO: REMOVE DEBUG - Print all channels for debugging (every 10 seconds to reduce spam)
    static unsigned long lastPowerPrint = 0;
    if ((now - lastPowerPrint) >= 10000) {
      lastPowerPrint = now;
      Serial.print("Power (all ");
      Serial.print(NUM_CHANNELS);
      Serial.print(" channels): ");
      for (int i = 0; i < NUM_CHANNELS; i++) {
        Serial.print("Ch");
        Serial.print(i);
        Serial.print("=");
        Serial.print(channelPowerPercent[i]);
        Serial.print("%");
        if (i < NUM_CHANNELS - 1) Serial.print(", ");
      }
      Serial.println();
    }
  }
  
  Particle.process();
}

// Helper function to round to 2 decimal places (matching main firmware)
double round2(double value) {
  return (int)(value * 100 + 0.5) / 100.0;
}

// Process power data received from COTS firmware over Serial1
// Message format: {"h":"power", "p":[ch0, ch1, ..., ch15]}
// Power data is sent every 250ms from COTS firmware
// Power values match channel status "p" field (bitToPercent(GetOutputValue()))
void processPowerData() {
  // Check Serial1 for incoming data
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n' || c == '\r') {
      if (serial1Buffer.length() > 0) {
        // Try to parse JSON
        DynamicJsonDocument doc(1024);  // Increased size for 16 channels
        DeserializationError error = deserializeJson(doc, serial1Buffer);
        
        if (!error && doc["h"] == "power") {
          // Valid power data message - matches channel status "p" field format
          if (doc.containsKey("p") && doc["p"].is<JsonArray>()) {
            JsonArray powerArray = doc["p"];
            int arraySize = powerArray.size();
            
            // Update power percentages for all channels (0-15)
            int channelsUpdated = 0;
            for (int i = 0; i < NUM_CHANNELS && i < arraySize; i++) {
              if (powerArray[i].is<double>()) {
                channelPowerPercent[i] = powerArray[i].as<double>();
                
                // Clamp to 0-100%
                if (channelPowerPercent[i] < 0.0) channelPowerPercent[i] = 0.0;
                if (channelPowerPercent[i] > 100.0) channelPowerPercent[i] = 100.0;
                channelsUpdated++;
              }
            }
            
            // TODO: REMOVE DEBUG - Log if we didn't receive all expected channels
            if (arraySize < NUM_CHANNELS) {
              Serial.print("Warning: Received ");
              Serial.print(arraySize);
              Serial.print(" channels, expected ");
              Serial.println(NUM_CHANNELS);
            }
          } else {
            // TODO: REMOVE DEBUG - Missing power array warning
            Serial.println("Warning: Power message missing 'p' array");
          }
        } else if (!error) {
          // Not a power message, might be other data
          // Serial.println("Received non-power message");
        } else {
          // TODO: REMOVE DEBUG - JSON parse error
          Serial.print("JSON parse error: ");
          Serial.println(error.c_str());
        }
        
        serial1Buffer = ""; // Clear buffer
      }
    } else {
      serial1Buffer += c;
      
      // Prevent buffer overflow
      if (serial1Buffer.length() > 1024) {  // Increased buffer size
        serial1Buffer = "";
      }
    }
  }
}


