# Serial Communication Upgrade - Complete Summary

## Overview
Upgraded the serial communication between FreqConverter and Veil-Line-Phase-1-Firmware to match the HBC-Simulation/HBC-COTS standard, with additional improvements including RPM-based communication, filtering, and fault code support.

---

## Phase 1: Communication Infrastructure Upgrade

### Created Files

#### FreqConverter
- **`src/FreqCommsManager/FreqCommsManager.h`**
  - Fixed-size char array buffers (2048 bytes) - no heap allocation
  - Char-by-char buffering with `$` delimiter
  - Message type extraction from `"h"` field
  - `ClearBuffers()` method for recovery
  - `ParseExpectedRpmJson()` - parses expected RPM messages from Veil-Line
  - `WriteSerialMessage()` - standardized message sending

#### Veil-Line-Phase-1-Firmware
- **`src/FreqCommsManager/FreqCommsManager.h`**
  - Same implementation as FreqConverter version
  - `ParseRollerStatusJson()` - parses roller status messages with RPM and fault codes
  - Returns `RollerStatusData` struct with `rpmArray` and `faultArray`

### Modified Files

#### FreqConverter
- **`src/FreqConverter.ino`**
  - Added `FreqCommsManager` instance for Serial1
  - Replaced `String` buffers with fixed `char` arrays
  - Replaced `JsonDocument` with `StaticJsonDocument` (no heap allocation)
  - Initialized manager in `setup()` with `ClearBuffers()`

#### Veil-Line-Phase-1-Firmware
- **`src/VeilControl.ino`**
  - Added two `FreqCommsManager` instances (Serial1 and Serial2)
  - Removed old `String` buffers (`externalStatusString1`, `externalStatusString2`)
  - Replaced inline parsing with manager-based approach
  - Removed code duplication (single manager handles both ports)

---

## Phase 2: RPM-Based Communication & Filtering

### Key Changes

#### 1. **FreqConverter - RPM Calculation & Filtering**

**Added:**
- `FrequencyToRpm()` function - converts frequency (Hz) to RPM using formula: `frequency * 60.0 / 400.0 / 20.0`
- Per-encoder tracking arrays:
  - `expectedRpm[]` - Last known expected RPM from Veil-Line
  - `calculatedRpm[]` - Calculated RPM from frequency
  - `rollerStatusRpm[]` - Final RPM to send (based on fault code)
  - `successCount[]` - Success count per encoder
  - `failCount[]` - Fail count per encoder
  - `faultCode[]` - Fault code per encoder

**Acceptance Logic (±20%):**
- Every 50ms: Calculate RPM from frequency, compare to expected RPM
- If calculated RPM within ±20% of expected → increment `successCount`
- If outside ±20% → increment `failCount`
- Counts reset every 1000ms when sending

**Fault Code Calculation:**
- `CalculateFaultCode()` function
- Fault code = 0 if fail percentage ≤ 50%
- Fault code = 1 if fail percentage > 50%
- Calculated every 1000ms when sending

**Roller Status RPM Selection:**
- **Fault code = 0 (no fault):** `rollerStatusRpm[i] = expectedRpm[i]`
- **Fault code != 0 (fault):** `rollerStatusRpm[i] = calculatedRpm[i]` (shows actual deviation)

**Message Format:**
```json
{
  "h": "rollerStatus",
  "rollerStatusArr": [rpm0, rpm1, rpm2, ...],
  "faultArr": [0, 1, 0, ...]
}$
```

**Timing:**
- Expected RPM check: Every 50ms (in `loop()`)
- RPM calculation/filtering: Every 50ms (`statusInterval`)
- Roller status send: Every 1000ms (`rollerStatusSendInterval`)

#### 2. **Veil-Line - Expected RPM Sending & RPM Reception**

**Added:**
- `sendExpectedRpm()` function - sends expected RPM every 50ms
- `processRollerStatusData()` function - processes RPM values and fault codes
- `encoderFaultCodes[]` array - stores fault codes (12 total: 10 encoders + 2 tachometers)

**Expected RPM Message:**
- **Serial1 (Particle 1):** Encoders 0-5 (6 values)
- **Serial2 (Particle 2):** Encoders 6-9, Tachometers 10-11 (6 values)
- Format: `{"h":"expectedRpm", "rpmArr":[rpm0, rpm1, ...]}$`
- Sent every 50ms to both serial ports

**Roller Status Reception:**
- Receives `{"h":"rollerStatus", "rollerStatusArr":[...], "faultArr":[...]}`
- Updates encoders/tachometers with RPM directly
- Stores fault codes for future use

#### 3. **Encoder Class Simplification**

**Added:**
- `Encoder::UpdateRpm(double rpm)` - new method that receives RPM directly
- No frequency conversion needed
- No filtering needed (done in FreqConverter)
- Updates averages with received RPM

**Kept:**
- `Encoder::Update(frequency, expectedRpm)` - legacy method for backward compatibility

#### 4. **WebSpeed Class Update**

**Added:**
- `WebSpeed::UpdateRpm(double rpm)` - receives RPM directly
- Updates RPM, FPM, and accumulation
- No frequency conversion needed

---

## Communication Flow

### Bidirectional Communication

**Veil-Line → FreqConverter (Every 50ms):**
```
Veil-Line sends: {"h":"expectedRpm", "rpmArr":[rpm0, rpm1, ...]}$
  ├─ Serial1 → Particle 1 (encoders 0-5)
  └─ Serial2 → Particle 2 (encoders 6-9, tach 10-11)
```

**FreqConverter → Veil-Line (Every 1000ms):**
```
FreqConverter sends: {"h":"rollerStatus", "rollerStatusArr":[rpm0, ...], "faultArr":[0, 1, ...]}$
  ├─ Particle 1 → Serial1 → Veil-Line (encoders 0-5)
  └─ Particle 2 → Serial2 → Veil-Line (encoders 6-9, tach 10-11)
```

### Data Processing Flow

1. **Veil-Line calculates expected RPM** from roller settings
2. **Veil-Line sends expected RPM** to FreqConverter every 50ms
3. **FreqConverter receives expected RPM** and stores it
4. **FreqConverter reads frequency** from hardware (continuous, interrupt-based)
5. **FreqConverter calculates RPM** from frequency every 50ms
6. **FreqConverter compares** calculated vs expected (±20% acceptance)
7. **FreqConverter tracks** success/fail counts
8. **FreqConverter calculates fault codes** every 1000ms (>50% fail = fault code 1)
9. **FreqConverter selects RPM** to send:
   - Fault code 0: Use expectedRpm
   - Fault code != 0: Use calculatedRpm (shows deviation)
10. **FreqConverter sends roller status** to Veil-Line every 1000ms
11. **Veil-Line receives RPM** and updates encoders/tachometers directly
12. **Veil-Line stores fault codes** for future use

---

## Message Formats

### From Veil-Line to FreqConverter
```json
{
  "h": "expectedRpm",
  "rpmArr": [rpm0, rpm1, rpm2, rpm3, rpm4, rpm5]
}$
```
- Serial1: Encoders 0-5
- Serial2: Encoders 6-9, Tachometers 10-11

### From FreqConverter to Veil-Line
```json
{
  "h": "rollerStatus",
  "rollerStatusArr": [rpm0, rpm1, rpm2, rpm3, rpm4, rpm5],
  "faultArr": [0, 1, 0, 0, 0, 1]
}$
```
- Serial1: Encoders 0-5
- Serial2: Encoders 6-9, Tachometers 10-11

---

## Fault Code System

### Current Implementation
- **Fault Code 0:** No fault (fail percentage ≤ 50%)
- **Fault Code 1:** High failure rate (fail percentage > 50%)

### Calculation
- Calculated every 1000ms when sending roller status
- Based on success/fail counts accumulated over 1000ms window
- Formula: `failPercentage = (failCount / (successCount + failCount)) * 100`
- Threshold: >50% failures = fault code 1

### Usage
- Fault codes stored in `encoderFaultCodes[]` array in Veil-Line
- Currently stored but not actively used (for future implementation)
- Affects which RPM value is sent:
  - Fault code 0: Send expectedRpm (normal operation)
  - Fault code 1: Send calculatedRpm (shows actual deviation)

---

## Improvements Over Previous Implementation

### 1. **No Heap Allocation**
- ✅ Fixed-size char arrays instead of `String`
- ✅ `StaticJsonDocument` instead of `JsonDocument`
- ✅ Reduces memory fragmentation on long-running Particle devices

### 2. **Standardized Communication**
- ✅ Matches HBC-Simulation/HBC-COTS pattern
- ✅ Consistent message format with `"h"` header field
- ✅ `$` delimiter at end of messages
- ✅ Dedicated CommsManager class

### 3. **Better Error Handling**
- ✅ Buffer overflow protection (discard and reset)
- ✅ `ClearBuffers()` for recovery
- ✅ Fault code system for tracking issues

### 4. **Centralized Filtering**
- ✅ All filtering logic in FreqConverter
- ✅ Veil-Line receives clean RPM values
- ✅ Simplified Encoder/WebSpeed classes

### 5. **Bidirectional Communication**
- ✅ Veil-Line sends expected RPM
- ✅ FreqConverter uses expected RPM for validation
- ✅ Enables intelligent filtering and fault detection

### 6. **Code Quality**
- ✅ Removed code duplication
- ✅ Consistent naming (`rollerStatus`, `rollerStatusRpm`, etc.)
- ✅ Clear separation of concerns

---

## File Changes Summary

### FreqConverter
- ✅ `src/FreqCommsManager/FreqCommsManager.h` (created)
- ✅ `src/FreqConverter.ino` (major updates)

### Veil-Line-Phase-1-Firmware
- ✅ `src/FreqCommsManager/FreqCommsManager.h` (created)
- ✅ `src/VeilControl.ino` (major updates)
- ✅ `src/Encoder/Encoder.h` (added `UpdateRpm()`)
- ✅ `src/Encoder/Encoder.cpp` (added `UpdateRpm()` implementation)
- ✅ `src/WebSpeed/WebSpeed.h` (added `UpdateRpm()`)
- ✅ `src/WebSpeed/WebSpeed.cpp` (added `UpdateRpm()` implementation)

---

## Timing Summary

| Operation | Frequency | Location |
|-----------|-----------|----------|
| Frequency reading | Continuous (interrupt) | FreqConverter |
| Expected RPM send | 50ms | Veil-Line |
| Expected RPM receive | 50ms | FreqConverter |
| RPM calculation/filtering | 50ms | FreqConverter |
| Roller status send | 1000ms | FreqConverter |
| Roller status receive | 1000ms | Veil-Line |
| Fault code calculation | 1000ms | FreqConverter |
| Success/fail count reset | 1000ms | FreqConverter |

---

## Key Design Decisions

1. **Filtering in FreqConverter:** All frequency→RPM conversion and filtering happens in FreqConverter, making it self-contained
2. **Fault-based RPM selection:** When in fault, send actual calculated RPM to show deviation; when normal, send expected RPM
3. **50% failure threshold:** Fault code 1 triggered when >50% of samples fail acceptance test
4. **Separate messages per particle:** Each FreqConverter particle receives only the 6 values it handles
5. **Fault codes stored but unused:** Ready for future implementation of fault handling logic

---

## Testing Checklist

- [ ] Verify FreqConverter receives expected RPM from Veil-Line
- [ ] Verify FreqConverter calculates RPM correctly from frequency
- [ ] Verify ±20% acceptance logic works correctly
- [ ] Verify fault code calculation (>50% threshold)
- [ ] Verify rollerStatusRpm selection (expectedRpm when fault=0, calculatedRpm when fault=1)
- [ ] Verify Veil-Line receives roller status messages
- [ ] Verify encoders update correctly with RPM values
- [ ] Verify tachometers update correctly with RPM values
- [ ] Verify fault codes are stored correctly
- [ ] Verify communication works on both Serial1 and Serial2
- [ ] Verify no memory leaks (heap usage stable over time)

---

## Future Enhancements (Not Implemented)

1. **Additional Fault Codes:**
   - Fault code 2: No signal / sensor timeout
   - Fault code 3: Communication loss
   - Fault code 4: Sensor disconnected

2. **Fault Code Usage:**
   - Alarm triggers based on fault codes
   - Shutdown conditions
   - Diagnostic reporting

3. **Tachometer Expected RPM:**
   - Currently sends 0.0 for tachometers
   - Could calculate from web speed or other sources

---

## Notes

- All changes maintain backward compatibility where possible
- Legacy `Encoder::Update(frequency, expectedRpm)` method kept for compatibility
- No timeout mechanism (removed per user request)
- Particle ID removed from messages (per user request)
- All code follows HBC communication standard pattern

