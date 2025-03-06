# HLK-LD2413 Component for ESPHome

This component provides integration with the HLK-LD2413 24GHz millimeter wave radar liquid level detection sensor for ESPHome using UART. This is a custom component created by me - use at your own risk. If you use or modify this component, please link back to the original repository.

## Features

-   High-precision liquid level detection using 24GHz millimeter wave radar
-   Detection range: 0.25m to 10.5m (datasheet says 150mm minimum, but in reality it doesn't report distances under 250mm)
-   Accuracy: ±3mm under optimal conditions
-   UART communication at 115200 baud
-   Configurable detection range and reporting cycle
-   Real-time distance measurements

## Hardware Setup

Connect your HLK-LD2413 sensor to your ESP32 using the following pins:

-   HLK-LD2413 TX (OT1) → ESP32 RX (GPIO16 in example)
-   HLK-LD2413 RX (RX) → ESP32 TX (GPIO17 in example)
-   HLK-LD2413 GND → ESP32 GND
-   HLK-LD2413 3V3 → ESP32 3.3V

## Configuration Variables

-   **uart_id** (_Required_, ID): The ID of the UART bus
-   **min_distance** (_Optional_, distance, default: 250mm): Minimum detection distance (valid range: 250mm to 10500mm)
-   **max_distance** (_Optional_, distance, default: 10500mm): Maximum detection distance (valid range: 250mm to 10500mm and has to be greater than min_distance)
-   **report_cycle** (_Optional_, time, default: 160ms): Sensor reporting cycle (valid range: 50ms to 1000ms). Higher values use less power
-   **update_interval** (_Required_, time): How often to poll the sensor and publish state updates. Set it to approximately 15x the report_cycle value, ie at 160ms report_cycle, the sensor provides a new value every 2.4s

## Basic Configuration

```yaml
uart:
    id: uart_bus
    tx_pin: GPIO17
    rx_pin: GPIO16
    baud_rate: 115200

sensor:
    - platform: hlk_ld2413
      uart_id: uart_bus
      name: "Water Level"
      update_interval: 2.4s
      # Optional configurations below
      min_distance: 250mm
      max_distance: 3000mm
      report_cycle: 160ms
```

## Installation

There are two ways to install this component:

### Option 1: Direct from GitHub (Recommended)

Add the following to your ESPHome configuration:

```yaml
external_components:
    - source: github://Averyy/esphome-custom-components
      components: [hlk_ld2413]
```

**Note:** Using the GitHub repository version means you'll automatically get updates, but it could change at any moment. If stability is critical for your project, consider using the local installation method.

### Option 2: Local Installation

If you prefer a more stable setup or need to modify the component:

1. Create a `components` directory in your ESPHome configuration directory
2. Copy the `hlk_ld2413` directory into the `components` directory
3. Add the `external_components` section to your YAML configuration:

```yaml
external_components:
    - source: components
```

## Mounting Recommendations

For optimal performance:

-   Mount the sensor perpendicular to the liquid surface
-   Avoid installation above feed inlets
-   Maintain a minimum safe distance of 1/5 of the maximum detection distance from walls
-   Avoid obstructions within the ±12° beam range

## Power Consumption

The sensor's power consumption varies based on the reporting cycle:

-   50ms: ~40mA
-   160ms (default): ~23mA
-   1000ms: ~16mA

## Notes

-   The sensor uses FMCW (Frequency-Modulated Continuous Wave) radar technology
-   The beam width is ±12° at -6dB (two-way)
-   The component automatically handles the sensor's communication protocol
-   For best accuracy, ensure the sensor is securely mounted to prevent false readings
