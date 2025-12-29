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

// Power data received from COTS firmware over Serial1
// Power percentages are sent every 250ms from COTS firmware

// Initial simulated TC values
// Set them all to 70.00 Deg F
double simulatedTCValues[8] = {
  70.0,  // TC 0
  70.0,  // TC 1
  70.0,  // TC 2
  70.0,  // TC 3
  70.0,  // TC 4
  70.0,  // TC 5
  70.0,  // TC 6
  70.0   // TC 7
};

// Power percentage values received from COTS firmware (channels 0-3)
// Updated when power messages are received over Serial1
double channelPowerPercent[4] = {0.0, 0.0, 0.0, 0.0};

// Thermal simulation constants
#define HEAT_CAP 1000.0        // Heat capacity (W*sec/°C)
#define HEAT_TRANSFER_HA 50.0  // Heat transfer coefficient (W/°C)
#define AMBIENT_TEMP 70.0      // Ambient temperature (°F)
#define MAX_POWER 1000.0       // Maximum power (W) - used to convert percentage to actual power

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
  
  delay(1000); // Give Serial1 time to initialize
  
  Serial.println("TC Simulator Started");
  Serial.println("Sending TC data for indices 0-7");
  Serial.println("Receiving power data from COTS firmware over Serial1");
}

void loop() {
  unsigned long now = millis();
  
  // Process incoming power data from COTS firmware over Serial1
  processPowerData();
  
  // Update every second
  if ((now - lastUpdate) >= UPDATE_INTERVAL) {
    lastUpdate = now;
    
    // Simulate temperature values based on channel power percentages
    // TC 0,1 -> channel 0; TC 2,3 -> channel 1; TC 4,5 -> channel 2; TC 6,7 -> channel 3
    for (int i = 0; i < 8; i++) {
      int channelIndex = i / 2;  // Map TC index to channel index (0-3)
      double powerPercent = channelPowerPercent[channelIndex];
      
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
     Serial.print("Power: Ch0="); Serial.print(channelPowerPercent[0]);
     Serial.print("%, Ch1="); Serial.print(channelPowerPercent[1]);
     Serial.print("%, Ch2="); Serial.print(channelPowerPercent[2]);
     Serial.print("%, Ch3="); Serial.println(channelPowerPercent[3]);
  }
  
  Particle.process();
}

// Helper function to round to 2 decimal places (matching main firmware)
double round2(double value) {
  return (int)(value * 100 + 0.5) / 100.0;
}

// Process power data received from COTS firmware over Serial1
// Message format: {"h":"power", "p":[ch0, ch1, ch2, ch3]}
// Power data is sent every 250ms from COTS firmware
// Power values match channel status "p" field (bitToPercent(GetOutputValue()))
void processPowerData() {
  // Check Serial1 for incoming data
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n' || c == '\r') {
      if (serial1Buffer.length() > 0) {
        // Try to parse JSON
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, serial1Buffer);
        
        if (!error && doc["h"] == "power") {
          // Valid power data message - matches channel status "p" field format
          if (doc.containsKey("p") && doc["p"].is<JsonArray>()) {
            JsonArray powerArray = doc["p"];
            int arraySize = powerArray.size();
            
            // Update power percentages for channels 0-3
            for (int i = 0; i < 4 && i < arraySize; i++) {
              if (powerArray[i].is<double>()) {
                channelPowerPercent[i] = powerArray[i].as<double>();
                
                // Clamp to 0-100%
                if (channelPowerPercent[i] < 0.0) channelPowerPercent[i] = 0.0;
                if (channelPowerPercent[i] > 100.0) channelPowerPercent[i] = 100.0;
              }
            }
          }
        }
        
        serial1Buffer = ""; // Clear buffer
      }
    } else {
      serial1Buffer += c;
      
      // Prevent buffer overflow
      if (serial1Buffer.length() > 512) {
        serial1Buffer = "";
      }
    }
  }
}

