# HLK-LD2413 Component for ESPHome

This component provides integration with the HLK-LD2413 24GHz millimeter wave radar liquid level detection sensor for ESPHome using UART. This is a custom component created by me - use at your own risk. If you use or modify this component, please link back to the original repository.

## Features

-   High-precision liquid level detection using 24GHz millimeter wave radar
-   Detection range: 0.15m to 10.5m
-   Accuracy: ±3mm under optimal conditions
-   UART communication at 115200 baud
-   Configurable detection range and reporting cycle
-   Real-time distance measurements
-   Sensor configuration and calibration mode (disabled by default)

## Hardware Setup

Connect your HLK-LD2413 sensor to your ESP32 using the following pins (with the antenna of the HLK facing you ie pins on the left side, left to right):

-   HLK-LD2413 Pin1 3V3 → ESP32 3.3V
-   HLK-LD2413 Pin2 GND → ESP32 GND
-   HLK-LD2413 Pin3 TX (OT1) → ESP32 RX (GPIO16 in example)
-   HLK-LD2413 Pin4 RX (RX) → ESP32 TX (GPIO17 in example)
-   HLK-LD2413 Pin5 IO (OT2) → Not used

## Configuration Variables

-   **uart_id** (_Required_, ID): The ID of the UART bus
-   **min_distance** (_Optional_, distance, default: 150mm): Minimum detection distance (valid range: 150mm to 10500mm)
-   **max_distance** (_Optional_, distance, default: 10500mm): Maximum detection distance (valid range: 150mm to 10500mm and has to be greater than min_distance)
-   **report_cycle** (_Optional_, time, default: 160ms): Sensor reporting cycle (valid range: 50ms to 1000ms). Higher values use less power
-   **calibrate_on_boot** (_Optional_, boolean, default: false): Whether to perform threshold calibration during boot. Enable this after physical installation or if the sensor environment changes. Strongly recommended to run at least once, but can also be run on every single boot.
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
      min_distance: 150mm
      max_distance: 2000mm
      report_cycle: 160ms
      calibrate_on_boot: true
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

## Calibration

The HLK-LD2413 sensor requires threshold calibration for optimal performance:

-   **Initial Installation**: Set `calibrate_on_boot: true` for the first boot after installation
-   **Environment Changes**: Recalibrate if the sensor housing or surrounding environment changes
-   **Normal Operation**: After successful calibration, set `calibrate_on_boot: false` to avoid unnecessary calibrations

Calibration helps the sensor establish a baseline for background noise and improves measurement accuracy.

### Calibration Process

The calibration process:

1. Sends a calibration command to the sensor
2. Waits for a valid acknowledgment response
3. Automatically retries up to 3 times if no valid response is received
4. Reports success only when a valid acknowledgment is received

### Troubleshooting Calibration Issues

If calibration fails:

-   Check the UART wiring connections
-   Ensure the sensor has stable power (3.3V)
-   Try power cycling the sensor
-   Increase distance from potential interference sources
-   Check the logs for specific error messages
-   The component will automatically retry calibration

### Calibration Timing

Calibration is a critical step that requires proper timing:

-   The sensor needs time to process the calibration command
-   The component now waits longer (500ms) for a response
-   Multiple retries are performed with delays between attempts
-   The entire calibration process may take up to 2 seconds

## Configuration Process

During boot, the component will:

1. Enter configuration mode
2. Configure the minimum and maximum detection distances
3. Set the reporting cycle
4. Perform threshold calibration (if `calibrate_on_boot` is enabled)
5. Exit configuration mode
6. Begin normal measurement operations

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
-   After changing any configuration parameters, the sensor will be automatically reconfigured on boot

## UART Communication Protocol

Based on the HLK-LD2413 documentation, here's the detailed communication protocol:

### Frame Headers and Footers

**Data Frames** (device to ESP):

```
Header: 0xF4 0xF3 0xF2 0xF1
Footer: 0xF8 0xF7 0xF6 0xF5
```

**Command Frames** (ESP to device):

```
Header: 0xFD 0xFC 0xFB 0xFA
Footer: 0x04 0x03 0x02 0x01
```

### Protocol Structure

**Command Frame Format:**

```
- Frame Header (4 bytes)
- Data Length (2 bytes, little-endian)
- Command Word (2 bytes)
- Command Value/Parameters (variable length)
- Frame Footer (4 bytes)
```

**ACK Format** (device response to commands):

```
- Frame Header (4 bytes)
- Data Length (2 bytes)
- Original Command Word (2 bytes)
- Command Execution Status (2 bytes, 0 for success, other values for failure)
- Return Value (variable length, if any)
- Frame Footer (4 bytes)
```

**Data Frame Format** (distance reporting):

```
- Frame Header (4 bytes)
- Data Length (2 bytes)
- Distance Value (4 bytes, float type in little-endian)
- Frame Footer (4 bytes)
```

### Key Commands

| Command           | Value    | Description                           |
| ----------------- | -------- | ------------------------------------- |
| Enter Config Mode | `0x00FF` | Must be sent before any other command |
| Exit Config Mode  | `0x00FE` | Must be sent after configuration      |
| Set Min Distance  | `0x0074` | Configure minimum detection range     |
| Set Max Distance  | `0x0075` | Configure maximum detection range     |
| Update Threshold  | `0x0072` | Calibration command                   |
| Set Report Cycle  | `0x0071` | Configure data reporting frequency    |

### Communication Process

1. Enter command mode first
2. Send configuration commands
3. Exit command mode to resume data reporting
4. Device will continuously report distance data using the data frame format

### Data Handling Notes

-   The device uses little-endian format for all multi-byte values
-   Distance values are sent as 4-byte IEEE 754 floating-point numbers in millimeters
-   The device operates at 115200 baud rate, 1 stop bit, no parity

This information is critical for properly parsing the data frames and sending valid commands to the device.
