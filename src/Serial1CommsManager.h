#ifndef SERIAL1COMMSMANAGER_H
#define SERIAL1COMMSMANAGER_H

#include <vector>
#include <Arduino.h>
#include <string.h>
#include <ArduinoJson.h>

// Inline parser function (so this class can be used standalone)
namespace Serial1Parser {
  static std::vector<String> parseArguments(String argumentString, String delimiter, int bufferSize = 1024){
    // Allocate buffer on stack - limit size to prevent stack overflow
    if(bufferSize > 2048) bufferSize = 2048; // Cap at 2KB for safety
    
    // Check if string is too long before copying
    if(argumentString.length() >= bufferSize - 1) {
      // String too long - return empty vector
      return std::vector<String>();
    }
    
    char argumentBuf[bufferSize];
    argumentString.toCharArray(argumentBuf, bufferSize);
    
    char delimiterBuf[10];
    delimiter.toCharArray(delimiterBuf, 10);

    std::vector<String> result;
    char * pch;

    pch = strtok (argumentBuf, delimiterBuf);
    while (pch != NULL)
    {
      // CRITICAL FIX: Create a copy of the string instead of storing pointer
      // The pointer from strtok becomes invalid when buffer goes out of scope
      result.push_back(String(pch)); // String constructor copies the C-string
      pch = strtok (NULL, delimiterBuf);
    }
    return result;
  }
}

// Channel Power Data Structure - holds power percentage for a channel
struct ChannelPowerData {
  int channelIndex;    // Channel index (0-15)
  double powerPercent; // Power percentage (0-100)
};

// Memory Statistics Structure - holds memory monitoring data
struct MemoryStats {
  int totalFree;      // Total free memory (bytes)
  int largestBlock;  // Largest contiguous free block (bytes)
  float fragmentation; // Fragmentation percentage (0-100)
};

class Serial1CommsManager {
  public:
    // Maximum message buffer size to prevent memory exhaustion
    static const int MAX_MESSAGE_LENGTH = 2048;
    
    // Constructor
    Serial1CommsManager(Stream& _serial1):serial1(_serial1){}

    void Begin(unsigned long baudRate = 115200){
      // Note: On Particle, Serial1.begin() should be called separately
      // This method is kept for compatibility but Serial1.begin() must be called before use
      // serial1.begin(baudRate); // Uncomment if Stream has begin() method
    }

    // Update - reads data from Serial1 and parses it
    // Returns true if new data was received and parsed
    bool Update(){
      bool gotData = false;
      
      // Read all available characters and append to buffer
      while(serial1.available()){
        char c = serial1.read();
        
        // Skip carriage return and newline (from println())
        if(c == '\r' || c == '\n'){
          continue;
        }
        
        // If we find the delimiter, process the message
        if(c == '$'){
          gotData = true;
          if(messageBuffer.length() > 0){
            // Store the raw message for debugging
            lastRawMessage = messageBuffer;
            
            // Detect message type: JSON starts with '{', CSV starts with number or comma
            if(messageBuffer.length() > 0 && messageBuffer.charAt(0) == '{'){
              // JSON message - parse and extract header
              parseJsonMessage(messageBuffer);
            } else {
              // Legacy CSV format - parse comma-separated values
              parsedData = Serial1Parser::parseArguments(messageBuffer, ",");
              messageType = ""; // Clear message type for CSV
            }
            
            messageBuffer = ""; // Clear buffer for next message
          }
        } else {
          // MEMORY FIX: Check buffer length to prevent unbounded growth
          // If buffer is too long, discard and reset to prevent memory exhaustion
          if(messageBuffer.length() >= MAX_MESSAGE_LENGTH){
            // Buffer overflow protection - discard malformed message
            messageBuffer = "";
            // Could optionally set an error flag here
            continue;
          }
          // Accumulate characters until we find '$'
          messageBuffer += c;
        }
      }
      
      return gotData;
    }
    
    // Parse JSON message and extract header type
    void parseJsonMessage(String jsonString){
      // Use StaticJsonDocument to avoid heap allocation
      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, jsonString);
      
      if(error){
        // JSON parsing failed - treat as unknown
        messageType = "";
        parsedData.clear();
        return;
      }
      
      // Extract message type from "h" field
      if(doc.containsKey("h")){
        messageType = doc["h"].as<String>();
      } else {
        messageType = "";
      }
      
      // Store the JSON document for later parsing
      lastJsonMessage = jsonString;
    }
    
    // Get the last raw message received (before parsing)
    String GetLastRawMessage(){
      return lastRawMessage;
    }

    // Get the parsed data as a vector of strings
    std::vector<String> GetParsedData(){
      return parsedData;
    }

    // Get parsed data at specific index
    String GetDataAt(int index){
      if(index >= 0 && index < (int)parsedData.size()){
        return parsedData[index];
      }
      return "";
    }

    // Get number of parsed data elements
    int GetDataCount(){
      return parsedData.size();
    }

    // Get the message type (header "h" field from JSON, or empty for CSV)
    String GetMessageType(){
      return messageType;
    }
    
    // Parse power data from JSON message
    // Expected JSON format: {"h":"power","p":[val1,val2,...]}
    // Returns vector of ChannelPowerData structures
    std::vector<ChannelPowerData> ParsePowerDataJson(int maxChannels = 16){
      std::vector<ChannelPowerData> channelPowerArray;
      
      if(messageType != "power" || lastJsonMessage.length() == 0){
        return channelPowerArray; // Not a power message
      }
      
      // Parse JSON
      StaticJsonDocument<256> doc; // Power messages are small, 256 should be enough
      DeserializationError error = deserializeJson(doc, lastJsonMessage);
      
      if(error || !doc.containsKey("p")){
        return channelPowerArray; // Invalid JSON or missing "p" array
      }
      
      JsonArray powerArray = doc["p"];
      int numChannels = powerArray.size();
      if(numChannels > maxChannels) numChannels = maxChannels;
      
      for(int ch = 0; ch < numChannels; ch++){
        ChannelPowerData chPower;
        chPower.channelIndex = ch;
        chPower.powerPercent = powerArray[ch].as<float>();
        channelPowerArray.push_back(chPower);
      }
      
      return channelPowerArray;
    }
    
    // Parse memory statistics from JSON message
    // Expected JSON format: {"h":"memory","mT":...,"mL":...,"mF":...}
    // Returns MemoryStats structure, or returns false if not a memory message
    bool ParseMemoryStatsJson(MemoryStats& stats){
      if(messageType != "memory" || lastJsonMessage.length() == 0){
        return false; // Not a memory message
      }
      
      // Parse JSON
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, lastJsonMessage);
      
      if(error){
        return false; // Invalid JSON
      }
      
      // Extract memory statistics
      if(doc.containsKey("mT")) stats.totalFree = doc["mT"].as<int>();
      else return false;
      
      if(doc.containsKey("mL")) stats.largestBlock = doc["mL"].as<int>();
      else return false;
      
      if(doc.containsKey("mF")) stats.fragmentation = doc["mF"].as<float>();
      else return false;
      
      return true;
    }
    
    // Parse 16 channel power data from CSV format (legacy support)
    // Expected format: channel0Power,channel1Power,...,channel15Power
    // Returns vector of ChannelPowerData structures
    std::vector<ChannelPowerData> ParseChannelPowerData(int maxChannels = 16){
      std::vector<ChannelPowerData> channelPowerArray;
      
      // Parse power values - each value is a channel's power percentage
      int numChannels = parsedData.size();
      if(numChannels > maxChannels) numChannels = maxChannels;
      
      for(int ch = 0; ch < numChannels; ch++){
        ChannelPowerData chPower;
        chPower.channelIndex = ch;
        chPower.powerPercent = parsedData[ch].toFloat();
        channelPowerArray.push_back(chPower);
      }
      
      return channelPowerArray;
    }

    /* Class Specific Functions */
    bool SendStatus(String statusString){
      serial1.println(statusString);
      return true;
    }

    int WriteSerialMessage(String message){
      serial1.println(message);
      return 1;
    }

  private:
    Stream& serial1;
    std::vector<String> parsedData;
    String lastRawMessage;  // Store last raw message for debugging
    String messageBuffer;   // Buffer for accumulating message characters
    String messageType;     // Message type from JSON "h" field (e.g., "power", "memory")
    String lastJsonMessage; // Store last JSON message for parsing
};
#endif

