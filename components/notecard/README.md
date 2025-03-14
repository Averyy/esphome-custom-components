# Notecard Component for ESPHome

This component provides integration with Blues Wireless Notecard (both cellular and WiFi variants) for ESPHome using UART. This is a custom component created by me - use at your own risk. If you use or modify this component, please link back to the original repository.

## Features

-   Supports both Cellular and WiFi Notecards
-   UART communication at 9600 baud (8N1)
-   Configurable data collection and sync intervals
-   Automatic data batching and syncing
-   JSON data handling
-   Access to Notecard temperature and battery voltage

## Hardware Setup

Connect your Notecard to your ESP32 using the following pins (assuming the Notecard is providing the power):

-   Notecard RX → ESP32 TX (GPIO17 in example)
-   Notecard TX → ESP32 RX (GPIO16 in example)
-   Notecard GND → ESP32 GND
-   Notecard VIO → ESP32 3.3V

## Configuration Variables

-   **uart_id** (_Required_, ID): The ID of the UART bus
-   **project_id** (_Required_, string): Your Notehub project ID
-   **sync_interval** (_Optional_, time, default: 4h): How often to sync batched sensor data to Notehub. Will also set the inbound interval and location update frequency to the same value to conserve battery life. If you use a manual sync set this to a higher value.
-   **org** (_Optional_, string): Sets the organization name for WiFi AP (only used for WiFi Notecards)

## Basic Configuration

```yaml
uart:
    id: uart_notecard
    tx_pin: GPIO17
    rx_pin: GPIO16
    baud_rate: 9600

notecard:
    id: notecard_component
    uart_id: uart_notecard
    project_id: "com.company.project"
    org: "MyOrganization"
    sync_interval: 4h
```

## Installation

There are two ways to install this component:

### Option 1: Direct from GitHub (Recommended)

Add the following to your ESPHome configuration:

```yaml
external_components:
    - source: github://Averyy/esphome-custom-components
      components: [notecard]
```

**Note:** Using the GitHub repository version means you'll automatically get updates, but it could change at any moment. If stability is critical for your project, consider using the local installation method.

### Option 2: Local Installation

If you prefer a more stable setup or need to modify the component:

1. Create a `components` directory in your ESPHome configuration directory
2. Copy the `notecard` directory into the `components` directory
3. Add the `external_components` section to your YAML configuration:

```yaml
external_components:
    - source: components
```

## Sending Data Example

This example shows using a simple linear hall effect sensor to read the magnetic field voltage value and send it to the Notecard. It's added to a new note file, which the Notecard will automatically add a timestamp to.

```yaml
sensor:
    - platform: adc
      pin: GPIO33
      name: "Hall Effect Raw Voltage"
      update_interval: 2s
      id: hall_voltage
      on_value:
          then:
              - lambda: |-
                    // Create JSON with sensor data
                    std::string json = "{";
                    json += "\"sensorVoltage\":" + to_string(x);
                    json += "}";
                    // Send data to Notecard
                    id(notecard_component).send_data(json);
```

## Including Notecard Temperature and Battery Voltage

The component provides methods to easily access the Notecard's internal temperature sensor and battery voltage. Easily add them to the note file with a few lines.

```yaml
sensor:
    - platform: adc
      pin: GPIO33
      name: "Hall Effect Raw Voltage"
      update_interval: 2s
      id: hall_voltage
      on_value:
          then:
              - lambda: |-
                    // Get values from Notecard
                    float temperature = id(notecard_component).get_notecard_temperature();
                    float battery_voltage = id(notecard_component).get_notecard_battery_voltage();
                    // Create JSON with all values
                    std::string json = "{";
                    json += "\"sensorVoltage\":" + to_string(x);
                    json += ",\"temperature\":" + to_string(temperature);
                    json += ",\"batteryVoltage\":" + to_string(battery_voltage);
                    json += "}";
                    // Send data to Notecard
                    id(notecard_component).send_data(json);
```

## Manual Sync Triggering

The component provides a `sync_now()` method that allows you to manually trigger a sync with Notehub without waiting for the scheduled sync interval. This is especially useful for battery-powered devices that wake up infrequently.

```yaml
sensor:
    - platform: adc
      pin: GPIO33
      name: "Hall Effect Raw Voltage"
      update_interval: 2s
      id: hall_voltage
      on_value:
          then:
              - lambda: |-
                    // Create JSON with sensor data
                    std::string json = "{";
                    json += "\"sensorVoltage\":" + to_string(x);
                    json += "}";

                    // Send data to Notecard
                    id(notecard_component).send_data(json);

                    // Trigger immediate sync with Notehub
                    id(notecard_component).sync_now();

                    // Give time for sync to process
                    delay(1500);
```

When using the `sync_now()` method:

-   Always check if it returns true to confirm the sync was triggered successfully
-   Add a short delay (1-2 seconds) after calling it to give the Notecard time to process the sync request
-   This is particularly useful in deep sleep applications where you want to ensure data is sent to Notehub before the device goes to sleep

## Notes

-   The Notecard component automatically configures the Notecard on startup
-   If the values are the same as the config, it won't reset them
-   It will never clear wifi credentials, so if you want to reset it and enter wifi AP mode after credentials have been stored, you push the button on the notecard. If no credentials exist, the notecard will automatically enter SoftAP mode.
-   For WiFi Notecards, the SoftAP will be named based on your organization name
