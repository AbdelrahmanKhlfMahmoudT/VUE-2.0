# Change Summary

This project has been updated to fix build/upload issues, add random telemetry values, and improve RPC handling. Below is a concise summary of the changes applied across files.

## EspCODE11.ino (Arduino IDE sketch)
- Added conditional PWM API support to work with both ESP32 Arduino Core 2.x and 3.x:
  - For Core 3.x: `ledcAttach(ENA, PWM_FREQ, PWM_RES)` and `ledcWrite(ENA, fanSpeed)`
  - For Core 2.x: `ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES)`, `ledcAttachPin(ENA, PWM_CHANNEL)`, and `ledcWrite(PWM_CHANNEL, fanSpeed)`
- Updated `processSetValue(int value)` to use the correct PWM call based on core version.
- Ensured setup closes properly and motor direction pins remain consistent (IN1=HIGH, IN2=LOW).

## src/main.cpp (PlatformIO firmware)
- Added random telemetry fields:
  - `nh3_ppm`: 6.00–7.00
  - `co2_ppm`: 1500–1550
  - `td`: 1200–1220
- Improved RPC handler response structure to include method name and value, and guard against missing `id`.
- Maintains FreeRTOS-based non-blocking RPC long-poll task.

## Telemetry / Networking
- Retains HTTP telemetry to ThingsBoard, including JSON building and content-type header.
- Serial logs include sensor snapshot and HTTP status for visibility.

## Notes
- If using Arduino IDE with ESP32 core 3.x, the sketch uses the new PWM API.
- If using PlatformIO (typically ESP32 core 2.x), the firmware uses the legacy PWM API.
- RPC failures with codes like `-11` indicate timeouts (no pending RPC) or intermittent DNS/network issues.
