#ifndef SERIAL1COMMSMANAGER_H
#define SERIAL1COMMSMANAGER_H

#include <vector>
#include <Arduino.h>
#include <string.h>

// Inline parser function (so this class can be used standalone)
namespace Serial1Parser {
  static std::vector<String> parseArguments(String argumentString, String delimiter, int bufferSize = 622){
    char argumentBuf[bufferSize];
    argumentString.toCharArray(argumentBuf, bufferSize);
    
    char delimiterBuf[10];
    delimiter.toCharArray(delimiterBuf, 10);

    std::vector<String> result;
    char * pch;

    pch = strtok (argumentBuf, delimiterBuf);
    while (pch != NULL)
    {
      result.push_back(pch);
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

class Serial1CommsManager {
  public:
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
            // Parse the received data into array/variables
            parsedData = Serial1Parser::parseArguments(messageBuffer, ",");
            messageBuffer = ""; // Clear buffer for next message
          }
        } else {
          // Accumulate characters until we find '$'
          messageBuffer += c;
        }
      }
      
      return gotData;
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

    // Parse 16 channel power data from parsed payload
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
};
#endif

