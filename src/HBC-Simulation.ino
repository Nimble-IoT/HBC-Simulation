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

// GPO Board Power Reading Configuration
// Pins D2, D3, D4, D5 read PWM signals from GPO board (channels 0-3)
#define GPO_PIN_0 D2  // Channel 0 power signal
#define GPO_PIN_1 D3  // Channel 1 power signal
#define GPO_PIN_2 D4  // Channel 2 power signal
#define GPO_PIN_3 D5  // Channel 3 power signal
#define PWM_FREQUENCY 10      // 10Hz PWM from main firmware
#define PWM_PERIOD_MS 100      // 100ms period (1/10Hz)
#define PWM_RESOLUTION 4095   // 12-bit resolution (0-4095)
#define PWM_MEASUREMENT_TIME_MS 200  // Measure over 2 periods for accuracy

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

// Power percentage values from GPO board (channels 0-3)
// Stored for future use to potentially modify TC values based on power
double channelPowerPercent[4] = {0.0, 0.0, 0.0, 0.0};

unsigned long lastUpdate = 0;
unsigned long lastPowerRead = 0;
#define POWER_READ_INTERVAL 100  // Read power every 100ms

void setup() {
  // Initialize Serial1 for communication with main Particle device
  Serial1.begin(SERIAL1_BAUD);
  
  // Initialize Serial (USB) for debugging (optional)
  Serial.begin(115200);
  
  // Wait for serial connection (optional, for debugging)
  // while (!Serial && millis() < 5000) {
  //   Particle.process();
  // }
  
  // Initialize GPO power reading pins as inputs
  pinMode(GPO_PIN_0, INPUT);
  pinMode(GPO_PIN_1, INPUT);
  pinMode(GPO_PIN_2, INPUT);
  pinMode(GPO_PIN_3, INPUT);
  
  delay(1000); // Give Serial1 time to initialize
  
  Serial.println("TC Simulator Started");
  Serial.println("Sending TC data for indices 0-7");
  Serial.println("Reading power from GPO board on D2-D5 (channels 0-3)");
}

void loop() {
  unsigned long now = millis();
  
  // Read power percentages from GPO board (channels 0-3)
  if ((now - lastPowerRead) >= POWER_READ_INTERVAL) {
    lastPowerRead = now;
    readGpoPowerLevels();
  }
  
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
    // Serial.print("Power: Ch0="); Serial.print(channelPowerPercent[0]);
    // Serial.print("%, Ch1="); Serial.print(channelPowerPercent[1]);
    // Serial.print("%, Ch2="); Serial.print(channelPowerPercent[2]);
    // Serial.print("%, Ch3="); Serial.println(channelPowerPercent[3]);
  }
  
  Particle.process();
}

// Helper function to round to 2 decimal places (matching main firmware)
double round2(double value) {
  return (int)(value * 100 + 0.5) / 100.0;
}

// Read PWM duty cycle from GPO board and convert to power percentage
// PWM is 10Hz (100ms period) with 12-bit resolution (0-4095)
// Formula: power% = (dutyCycle / 4095.0) * 100.0
void readGpoPowerLevels() {
  int gpoPins[4] = {GPO_PIN_0, GPO_PIN_1, GPO_PIN_2, GPO_PIN_3};
  
  for (int ch = 0; ch < 4; ch++) {
    unsigned long highTime = 0;
    unsigned long measurementStart = millis();
    int highCount = 0;
    int totalSamples = 0;
    
    // Measure over multiple PWM periods for accuracy
    while ((millis() - measurementStart) < PWM_MEASUREMENT_TIME_MS) {
      if (digitalRead(gpoPins[ch]) == HIGH) {
        highCount++;
      }
      totalSamples++;
      delayMicroseconds(100); // Sample every 100us for good resolution
    }
    
    // Calculate duty cycle: (high samples / total samples) * resolution
    double dutyRatio = (double)highCount / (double)totalSamples;
    double dutyCycle = dutyRatio * PWM_RESOLUTION;
    
    // Convert to power percentage: (dutyCycle / 4095.0) * 100.0
    channelPowerPercent[ch] = (dutyCycle / 4095.0) * 100.0;
    
    // Clamp to 0-100%
    if (channelPowerPercent[ch] < 0.0) channelPowerPercent[ch] = 0.0;
    if (channelPowerPercent[ch] > 100.0) channelPowerPercent[ch] = 100.0;
  }
}

