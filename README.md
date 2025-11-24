# mop_tally
*ATEM ESP-NOW Tally System*

This repository contains two PlatformIO projects that together form a lightweight, wireless Tally system for Blackmagic ATEM switchers. A central ESP32-based server communicates with the ATEM, while multiple ESP8266-based Tally clients receive status updates via ESP-NOW broadcast.

## Overview
The system uses an ESP32-S3-ETH board as a server that connects to the ATEM via Ethernet and processes program/preview state. Tally devices receive their status wirelessly through ESP-NOW without the need for individual connections, increasing reliability and range.


---

## Features
- Central server with Ethernet connection to the ATEM
- ESP-NOW broadcast to all Tally clients (no pairing required)
- Web interface for configuration:
  - Set ATEM IP address
  - Adjust LED brightness for all Tally units
  - Map Tally hardware addresses (0x01–0x1F) to ATEM inputs
- Modular design, easy to extend
- ESP8266-based clients with simple hardware

---

## Project Structure
```
.
├── MOP Tallyserver/    # PlatformIO project for the ESP32-S3-ETH server
└── MOP Tallyclient/    # PlatformIO project for the ESP8266 Tally devices
```

---

## Server (ESP32-S3-ETH)
The server communicates with the Blackmagic ATEM switcher using its network protocol. It hosts a web interface where you can:
- Enter the ATEM IP address
- Configure LED brightness
- Assign input numbers to hardware Tally IDs

The server broadcasts Tally states to all devices via ESP-NOW.

---

## Client (ESP8266)
Each Tally unit is built using an ESP8266. A build guide can be found on GitHub (search for *Aron*'s Tally project).

Before flashing the firmware, **edit the hardware address** in the source code. Valid values are `0x01` to `0x1F`.

The client listens for ESP-NOW broadcast messages containing program/preview status and updates the LED indicators accordingly.

---

## ESP-NOW Broadcast
Instead of managing individual wireless connections, the server broadcasts all Tally state information using ESP-NOW. This brings several advantages:
- No pairing or Wi-Fi setup needed
- Increased range compared to Wi-Fi
- More robust in noisy environments
- Supports many devices without additional load

---

## Credits

The communication with the ATEM is based on the [Arduino libraries](https://github.com/kasperskaarhoj/SKAARHOJ-Open-Engineering/tree/master/ArduinoLibs) by Skaarhoj (SKAARHOJ-Open-Engineering), which have been slightly adapted for this project.

This project was developed in collaboration with **Medienoperative Ulm e.V.**

