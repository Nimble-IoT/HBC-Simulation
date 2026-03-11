/*
 * TC Simulator Firmware (Simplified)
 * Description: Emits thermocouple readings as JSON over Serial1.
 *              On startup it sends one full room-temperature snapshot for all TCs.
 *              After startup it only sends active TC indices 0-3 as triplets of
 *              (index, temperature, faultCode).
 */

SYSTEM_THREAD(ENABLED);

// Define connection options
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#include <ArduinoJson.h>
#include "Serial1CommsManager.h"

void appendTcTriplet(ArduinoJson::JsonArray& tcDataArray, int tcIndex);

// Serial1CommsManager instance
Serial1CommsManager serial1CommsManager(Serial1);

// Serial1 configuration (RX/TX pins on Particle Argon)
#define SERIAL1_BAUD 115200

// Update intervals
#define UPDATE_INTERVAL 1000  // Thermal simulation update (1 second)
#define TC_SEND_INTERVAL_MS 1000  // Send all active TCs once per second

// Initial simulated TC values
// Set them all to 70.00 Deg F
#define NUM_TCS 50
double simulatedTCValues[NUM_TCS];

// Power percentage values received from COTS firmware (channels 0-15)
// Updated when power messages are received over Serial1
#define NUM_CHANNELS 16
double channelPowerPercent[NUM_CHANNELS];

#define NUM_ACTIVE_TCS 4
#define NUM_CASES 2
#define STARTUP_ROOM_TEMP 70.0
#define DEFAULT_CASE_INDEX 0

unsigned long lastUpdate = 0;
unsigned long lastTcSend = 0;
int tcFaultCodes[NUM_TCS];  // TC fault codes array
int activeCaseIndex = DEFAULT_CASE_INDEX;

struct ActiveTcConfig {
  int tcIndex;
  int channelIndex;
  double heatCap;
  double heatTransferHa;
  double ambientTemp;
  double maxPower;
};

struct SimulationCase {
  int caseId;
  const char* caseName;
  ActiveTcConfig activeTcConfigs[NUM_ACTIVE_TCS];
};

// Hardcoded cases can be extended by editing these four active TC rows per case.
const SimulationCase simulationCases[NUM_CASES] = {
  {
    1,
    "case1",
    {
      {0, 0, 1000.0, 50.0, 70.0, 10000.0},
      {1, 1, 1000.0, 50.0, 70.0, 10000.0},
      {2, 2, 1000.0, 50.0, 70.0, 10000.0},
      {3, 3, 1000.0, 50.0, 70.0, 10000.0}
    }
  },
  {
    2,
    "case2",
    {
      {0, 4, 1200.0, 35.0, 70.0, 8500.0},
      {1, 5, 900.0, 60.0, 70.0, 9000.0},
      {2, 6, 1100.0, 45.0, 70.0, 9500.0},
      {3, 7, 800.0, 70.0, 70.0, 10500.0}
    }
  }
};

double round2(double value) {
  return (int)(value * 100 + 0.5) / 100.0;
}

const SimulationCase& getActiveCase() {
  return simulationCases[activeCaseIndex];
}

bool isActiveTcIndexValid(int tcIndex) {
  return tcIndex >= 0 && tcIndex < NUM_ACTIVE_TCS;
}

bool validateCase(const SimulationCase& simulationCase) {
  bool seenIndices[NUM_ACTIVE_TCS] = {false, false, false, false};

  for (int i = 0; i < NUM_ACTIVE_TCS; i++) {
    const ActiveTcConfig& activeTc = simulationCase.activeTcConfigs[i];

    if (!isActiveTcIndexValid(activeTc.tcIndex)) {
      return false;
    }

    if (activeTc.channelIndex < 0 || activeTc.channelIndex >= NUM_CHANNELS) {
      return false;
    }

    if (activeTc.heatCap <= 0.0 || activeTc.maxPower < 0.0) {
      return false;
    }

    if (seenIndices[activeTc.tcIndex]) {
      return false;
    }

    seenIndices[activeTc.tcIndex] = true;
  }

  return true;
}

int findCaseIndexByCommand(String command) {
  command.trim();
  command.toLowerCase();

  for (int i = 0; i < NUM_CASES; i++) {
    String caseName = simulationCases[i].caseName;
    caseName.toLowerCase();

    if (command == caseName) {
      return i;
    }

    if (command == String(simulationCases[i].caseId)) {
      return i;
    }
  }

  return -1;
}

void resetActiveTcValuesToAmbient() {
  const SimulationCase& simulationCase = getActiveCase();

  for (int i = 0; i < NUM_ACTIVE_TCS; i++) {
    const ActiveTcConfig& activeTc = simulationCase.activeTcConfigs[i];
    simulatedTCValues[activeTc.tcIndex] = activeTc.ambientTemp;
    tcFaultCodes[activeTc.tcIndex] = 0;
  }
}

bool applyCaseByIndex(int newCaseIndex, bool resetActiveTemps) {
  if (newCaseIndex < 0 || newCaseIndex >= NUM_CASES) {
    return false;
  }

  if (!validateCase(simulationCases[newCaseIndex])) {
    return false;
  }

  activeCaseIndex = newCaseIndex;

  if (resetActiveTemps) {
    resetActiveTcValuesToAmbient();
  }

  return true;
}

void appendTcTriplet(ArduinoJson::JsonArray& tcDataArray, int tcIndex) {
  tcDataArray.add(tcIndex);
  tcDataArray.add(round2(simulatedTCValues[tcIndex]));
  tcDataArray.add(tcFaultCodes[tcIndex]);
}

void sendFullTcSnapshot() {
  static StaticJsonDocument<2048> tcDoc;
  static char tcDataBuffer[2560];

  tcDoc.clear();
  tcDoc["h"] = "tc";
  JsonArray tcDataArray = tcDoc.createNestedArray("d");

  for (int i = 0; i < NUM_TCS; i++) {
    appendTcTriplet(tcDataArray, i);
  }

  serializeJson(tcDoc, tcDataBuffer, sizeof(tcDataBuffer));

  size_t len = strlen(tcDataBuffer);
  if (len < sizeof(tcDataBuffer) - 2) {
    tcDataBuffer[len] = '$';
    tcDataBuffer[len + 1] = '\0';
  }

  Serial.print("SEND TC INIT: ");
  Serial.println(tcDataBuffer);
  serial1CommsManager.WriteSerialMessage(tcDataBuffer);
}

void sendActiveTcData() {
  static StaticJsonDocument<256> tcDoc;
  static char tcDataBuffer[384];

  tcDoc.clear();
  tcDoc["h"] = "tc";
  JsonArray tcDataArray = tcDoc.createNestedArray("d");

  const SimulationCase& simulationCase = getActiveCase();
  for (int i = 0; i < NUM_ACTIVE_TCS; i++) {
    appendTcTriplet(tcDataArray, simulationCase.activeTcConfigs[i].tcIndex);
  }

  serializeJson(tcDoc, tcDataBuffer, sizeof(tcDataBuffer));

  size_t len = strlen(tcDataBuffer);
  if (len < sizeof(tcDataBuffer) - 2) {
    tcDataBuffer[len] = '$';
    tcDataBuffer[len + 1] = '\0';
  }

  Serial.print("SEND TC ACTIVE: ");
  Serial.println(tcDataBuffer);
  serial1CommsManager.WriteSerialMessage(tcDataBuffer);
}

int setSimulationCase(String command) {
  int newCaseIndex = findCaseIndexByCommand(command);

  if (newCaseIndex < 0) {
    Serial.print("Rejected case change: ");
    Serial.println(command);
    return -1;
  }

  if (!applyCaseByIndex(newCaseIndex, true)) {
    Serial.print("Invalid case config: ");
    Serial.println(command);
    return -1;
  }

  Serial.print("Active case changed to ");
  Serial.print(simulationCases[newCaseIndex].caseName);
  Serial.print(" (id=");
  Serial.print(simulationCases[newCaseIndex].caseId);
  Serial.println(")");
  return 1;
}

void setup() {
  // Initialize Serial1 for communication with main Particle device
  Serial1.begin(SERIAL1_BAUD);
  
  // Initialize Serial (USB) for debugging (optional)
  Serial.begin(115200);
  
  // Wait for serial connection (optional, for debugging)
  // while (!Serial && millis() < 5000) {
  //   Particle.process();
  // }
  
  // Initialize all TC values to room temperature for the startup snapshot.
  for (int i = 0; i < NUM_TCS; i++) {
    simulatedTCValues[i] = STARTUP_ROOM_TEMP;
  }
  
  // Initialize all channel power percentages to 0.0
  for (int i = 0; i < NUM_CHANNELS; i++) {
    channelPowerPercent[i] = 0.0;
  }
  
  // Initialize all TC fault codes to 0 (no fault)
  for (int i = 0; i < NUM_TCS; i++) {
    tcFaultCodes[i] = 0;
  }

  applyCaseByIndex(DEFAULT_CASE_INDEX, false);
  
  // Initialize Serial1CommsManager (Serial1.begin() already called above)
  serial1CommsManager.Begin(SERIAL1_BAUD);  // Begin() is now empty, kept for compatibility

  Particle.function("setCase", setSimulationCase);
  
  delay(1000); // Give Serial1 time to initialize
  
  Serial.println("TC Simulator Started (Async Mode)");
  Serial.print("Default case: ");
  Serial.println(getActiveCase().caseName);
  Serial.println("Sending startup TC snapshot for indices 0-49");
  Serial.println("Ongoing TC updates only include indices 0-3");
  Serial.print("Receiving power data for ");
  Serial.print(NUM_CHANNELS);
  Serial.println(" channels from main firmware over Serial1");

  sendFullTcSnapshot();
  lastUpdate = millis();
  lastTcSend = millis();
}

void loop() {
  unsigned long now = millis();
  
  // Update thermal simulation every second
  if ((now - lastUpdate) >= UPDATE_INTERVAL) {
    lastUpdate = now;

    const SimulationCase& simulationCase = getActiveCase();
    for (int i = 0; i < NUM_ACTIVE_TCS; i++) {
      const ActiveTcConfig& activeTc = simulationCase.activeTcConfigs[i];
      double powerPercent = channelPowerPercent[activeTc.channelIndex];
      double powerIn = (powerPercent / 100.0) * activeTc.maxPower;
      double temp = simulatedTCValues[activeTc.tcIndex];
      double tDotIn = powerIn / activeTc.heatCap;
      double tDotOut = (temp - activeTc.ambientTemp) * activeTc.heatTransferHa / activeTc.heatCap;
      double tDot = tDotIn - tDotOut;

      simulatedTCValues[activeTc.tcIndex] = temp + tDot * UPDATE_INTERVAL / 1000.0;
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
  
  // Send all active TCs together once per second.
  if((now - lastTcSend) >= TC_SEND_INTERVAL_MS){
    lastTcSend = now;
    sendActiveTcData();
  }
  
  Particle.process();
}
