# Pulse Oximeter IoT Dashboard

A DIY IoT pulse oximeter system built around an **ESP8266**, **MAX30100** sensor, and **OLED display**, with cloud logging through **ThingSpeak** and a public web dashboard deployed on **Vercel**.

This project measures:
- **Heart rate (BPM)**
- **Blood oxygen saturation (SpO2)**

It filters unstable scans, uploads only finalized valid readings, and displays historical data with timestamps through a custom browser dashboard.

## Project Overview

This system has three main parts:

1. **Embedded device**
   - ESP8266-based firmware reads the MAX30100 sensor
   - Detects finger presence and pulse beats
   - Collects BPM and SpO2 samples over a timed measurement window
   - Rejects unstable sessions and uploads only stable final values

2. **Cloud storage**
   - Final readings are sent to **ThingSpeak**
   - Each successful reading is stored with a cloud timestamp

3. **Web dashboard**
   - Hosted on **Vercel**
   - Reads historical values from ThingSpeak
   - Displays latest values, trend graphs, profile-based interpretation, and time-range views

## Features

- MAX30100-based heart rate and SpO2 sensing
- OLED measurement screen with heartbeat animation
- Stabilization and countdown workflow
- Outlier rejection and scan quality filtering
- Final median-based BPM and SpO2 calculation
- ThingSpeak upload for stable readings only
- Vercel-hosted dashboard with:
  - latest reading cards
  - timestamped history table
  - chart filters for `Today`, `7 days`, `30 days`, and `All`
  - profile-based interpretation using age, sex, weight, and activity context

## Repository Structure

```text
.
├── arduino_pulse_esp8266.ino
├── thingspeak_dashboard.html
├── vercel-dashboard/
│   ├── index.html
│   └── vercel.json
└── README.md
```

## Hardware Used

- ESP8266 development board
- MAX30100 pulse oximeter / heart-rate sensor
- 0.96" OLED display
- jumper wires / breadboard / power source

## Firmware

Main sketch:

- [arduino_pulse_esp8266.ino](D:/arduinopulse/arduino_pulse_esp8266.ino)

### What the firmware does

- connects to Wi-Fi
- reads pulse and SpO2 data from the MAX30100
- shows measurement status on the OLED
- collects multiple samples over a countdown period
- rejects unstable readings using filtering logic
- computes final BPM and SpO2 using recent accepted samples
- uploads the final stable result to ThingSpeak

### Before uploading the sketch

Replace these placeholders in the sketch:

```cpp
char ssid[] = "YOUR_WIFI_NAME";
char pass[] = "YOUR_WIFI_PASSWORD";
const char *thingspeakWriteApiKey = "YOUR_THINGSPEAK_WRITE_API_KEY";
```

## ThingSpeak Setup

Create a ThingSpeak channel with:

- `Field 1` = BPM
- `Field 2` = SpO2

Then copy:
- your **Channel ID**
- your **Write API Key**
- your **Read API Key** if the channel is private

The firmware uses the **Write API Key**.

The web dashboard uses:
- **Channel ID**
- optional **Read API Key**

## Dashboard

Dashboard files:

- [vercel-dashboard/index.html](D:/arduinopulse/vercel-dashboard/index.html)
- [vercel-dashboard/vercel.json](D:/arduinopulse/vercel-dashboard/vercel.json)

### Dashboard capabilities

- loads historical readings from ThingSpeak
- shows latest BPM and SpO2
- displays reading timestamps
- provides graph filters for different time windows
- includes a simple profile-based interpretation panel
- stores dashboard settings locally in the browser


