#ifndef SERIAL1COMMSMANAGER_H
#define SERIAL1COMMSMANAGER_H

#include <vector>
#include <Arduino.h>
#include <string.h>
#include <ArduinoJson.h>

// Channel Power Data Structure - holds power percentage for a channel
struct ChannelPowerData {
  int channelIndex;    // Channel index (0-15)
  double powerPercent; // Power percentage (0-100)
};


class Serial1CommsManager {
  public:
    // Constructor
    Serial1CommsManager(Stream& _serial1):serial1(_serial1), messageBufferIndex(0){
      messageBuffer[0] = '\0';
      lastRawBuffer[0] = '\0';
      lastJsonBuffer[0] = '\0';
      messageTypeBuffer[0] = '\0';
      lastJsonParseSucceeded = false;
    }

    void Begin(unsigned long baudRate = 115200){
      // Note: On Particle, Serial1.begin() should be called separately
      // This method is kept for compatibility but Serial1.begin() must be called before use
      // serial1.begin(baudRate); // Uncomment if Stream has begin() method
    }

    // Update - reads data from Serial1 and parses it
    // Returns true if new data was received and parsed
    // Messages are formatted as: data$ (delimiter at end only)
    bool Update(){
      bool gotData = false;
      
      while(serial1.available() > 0){
        char c = serial1.read();
        
        // Skip carriage return and newline
        if(c == '\r' || c == '\n'){
          continue;
        }
        
        // If we find the delimiter, process the message
        if(c == '$'){
          if(messageBufferIndex > 0){
            gotData = true;
            messageBuffer[messageBufferIndex] = '\0';
            strncpy(lastRawBuffer, messageBuffer, sizeof(lastRawBuffer) - 1);
            lastRawBuffer[sizeof(lastRawBuffer) - 1] = '\0';
            lastJsonParseSucceeded = false;
            
            // Parse JSON and extract header type
            if(messageBuffer[0] == '{'){
              parseJsonMessage(messageBuffer);
            } else {
              messageTypeBuffer[0] = '\0';
            }
            
            // Clear buffer for next message
            messageBufferIndex = 0;
            messageBuffer[0] = '\0';
          }
        } else {
          // Check buffer overflow
          if(messageBufferIndex >= (int)sizeof(messageBuffer) - 1){
            messageBufferIndex = 0;
            messageBuffer[0] = '\0';
            continue;
          }
          messageBuffer[messageBufferIndex++] = c;
        }
      }
      
      return gotData;
    }
    
    // Parse JSON message and extract header type
    void parseJsonMessage(const char* jsonString){
      // Power messages can contain 16 channels, so leave headroom for ArduinoJson metadata.
      static StaticJsonDocument<1024> doc;
      doc.clear();
      DeserializationError error = deserializeJson(doc, jsonString);
      
      if(error){
        // JSON parsing failed - treat as unknown
        messageTypeBuffer[0] = '\0';
        lastJsonBuffer[0] = '\0';
        lastJsonParseSucceeded = false;
        return;
      }
      
      // Extract message type from "h" field
      if(doc.containsKey("h")){
        const char* msgType = doc["h"].as<const char*>();
        if(msgType){
          strncpy(messageTypeBuffer, msgType, sizeof(messageTypeBuffer) - 1);
          messageTypeBuffer[sizeof(messageTypeBuffer) - 1] = '\0';
        } else {
          messageTypeBuffer[0] = '\0';
        }
      } else {
        messageTypeBuffer[0] = '\0';
      }
      
      // Store the JSON document for later parsing
      strncpy(lastJsonBuffer, jsonString, sizeof(lastJsonBuffer) - 1);
      lastJsonBuffer[sizeof(lastJsonBuffer) - 1] = '\0';
      lastJsonParseSucceeded = true;
    }
    
    // Clear all buffers and reset state
    void ClearBuffers(){
      messageBufferIndex = 0;
      messageBuffer[0] = '\0';
      lastRawBuffer[0] = '\0';
      messageTypeBuffer[0] = '\0';
      lastJsonBuffer[0] = '\0';
      lastJsonParseSucceeded = false;
      while(serial1.available() > 0){
        serial1.read();
      }
    }
    
    const char* GetLastRawMessageCStr(){
      return lastRawBuffer;
    }

    bool LastJsonParseSucceeded(){
      return lastJsonParseSucceeded;
    }

    // Get the message type (header "h" field from JSON)
    const char* GetMessageTypeCStr(){
      return messageTypeBuffer;
    }
    
    // Parse power data from JSON message
    // Expected JSON format: {"h":"power","p":[val1,val2,...]}
    // Returns vector of ChannelPowerData structures
    std::vector<ChannelPowerData> ParsePowerDataJson(int maxChannels = 16){
      std::vector<ChannelPowerData> channelPowerArray;
      
      if(strcmp(messageTypeBuffer, "power") != 0 || lastJsonBuffer[0] == '\0'){
        return channelPowerArray; // Not a power message
      }
      
      // 16-channel power arrays need more room than the previous 256-byte document allowed.
      static StaticJsonDocument<1024> doc;
      doc.clear();
      DeserializationError error = deserializeJson(doc, lastJsonBuffer);
      
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
    
    /* Class Specific Functions */
    bool SendStatus(String statusString){
      serial1.println(statusString);
      return true;
    }

    // Write message from String (for backward compatibility)
    int WriteSerialMessage(String message){
      serial1.println(message);
      // Removed Serial.flush() - it can block indefinitely if interface stops reading
      // Let the OS handle buffering instead
      return 1;
    }
    
    // Write message from const char* (preferred - no String allocation)
    int WriteSerialMessage(const char* message){
      serial1.println(message);
      // Removed Serial.flush() - it can block indefinitely if interface stops reading
      // Let the OS handle buffering instead
      return 1;
    }

  private:
    Stream& serial1;
    
    // Fixed-size buffers - no heap allocation (allocated once per instance)
    char messageBuffer[2048];         // Buffer for accumulating message characters
    int messageBufferIndex;            // Current write position in buffer
    char lastRawBuffer[2048];          // Store last complete raw message for debug
    char lastJsonBuffer[2048];         // Store last JSON message for parsing
    char messageTypeBuffer[16];        // Message type from JSON "h" field (e.g., "power", "tc")
    bool lastJsonParseSucceeded;       // True if the last raw message parsed as JSON
};
#endif

