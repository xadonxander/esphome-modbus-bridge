# ESPHome Modbus TCP ↔ RTU gateway (development)

**Status: experimental / not hardware verified. Do not deploy to a live battery/solar system yet.**

Custom ESPHome external component for a generic Modbus TCP server and UART/RS485 bridge. The initial source is being migrated from a local prototype. It is **not a validated release**.

## Hardware under development
- ESP32-S3 N16R8
- RS485 RX GPIO16 (previously used for passive sniffing)
- RS485 TX GPIO17 (proposed, unverified)
- RS485 direction GPIO18 (proposed, unverified)
- UART 115200 8N1
- TCP port 5020

## Safety
- Disconnect the BT-02 master before enabling active requests.
- Modbus writes can change battery charging settings. Do not expose the TCP port outside a trusted LAN.
- Preserve original firmware, API credentials, and OTA password before flashing.
- This repo is **not yet ready to reference from ESPHome**; the component must be published, built, and tested first.

The existing JK BMS Bluetooth configuration and Home Assistant ECO-WORTHY integration are separate pieces of the project.
