/*
 * TC Simulator Firmware
 * Description: Simulates 8 thermocouples (TCs 0-7) and sends data over Serial1
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

// Simulated TC values (steady values for testing)
// These can be modified to test different scenarios
double simulatedTCValues[8] = {
  75.0,  // TC 0
  80.0,  // TC 1
  85.0,  // TC 2
  90.0,  // TC 3
  95.0,  // TC 4
  100.0, // TC 5
  105.0, // TC 6
  110.0  // TC 7
};

unsigned long lastUpdate = 0;

void setup() {
  // Initialize Serial1 for communication with main Particle device
  Serial1.begin(SERIAL1_BAUD);
  
  // Initialize Serial (USB) for debugging (optional)
  Serial.begin(115200);
  
  // Wait for serial connection (optional, for debugging)
  // while (!Serial && millis() < 5000) {
  //   Particle.process();
  // }
  
  delay(1000); // Give Serial1 time to initialize
  
  Serial.println("TC Simulator Started");
  Serial.println("Sending TC data for indices 0-7");
}

void loop() {
  unsigned long now = millis();
  
  // Update every second
  if ((now - lastUpdate) >= UPDATE_INTERVAL) {
    lastUpdate = now;
    
    // Create JSON document for all 8 TCs
    DynamicJsonDocument tcDataDoc(1024);
    tcDataDoc["h"] = "tcSim"; // Header: thermocouple simulator
    
    // Create array of TC data
    JsonArray tcArray = tcDataDoc.createNestedArray("tcs");
    
    for (int i = 0; i < 8; i++) {
      JsonObject tcObj = tcArray.createNestedObject();
      tcObj["i"] = i;                    // Index
      tcObj["t"] = round2(simulatedTCValues[i]); // Temperature
      tcObj["fC"] = 0;                   // Fault code (0 = no fault)
    }
    
    // Serialize and send over Serial1
    String tcDataString;
    serializeJson(tcDataDoc, tcDataString);
    Serial1.println(tcDataString);
    
    // Optional: Also print to Serial for debugging
    // Serial.println("Sent: " + tcDataString);
  }
  
  Particle.process();
}

// Helper function to round to 2 decimal places (matching main firmware)
double round2(double value) {
  return (int)(value * 100 + 0.5) / 100.0;
}

