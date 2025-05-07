# HLK-LD8001H Component for ESPHome

This component provides integration with the HLK-LD8001H 80GHz millimeter wave radar liquid level detection sensor for ESPHome using MODBUS-RTU over UART. This is a custom component - use at your own risk.

## Features

-   High-precision liquid level detection using 80GHz millimeter wave radar
-   Detection range: 0.15m to 40m
-   Accuracy: ±5mm under optimal conditions
-   MODBUS-RTU communication at 115200 baud
-   Configurable installation height and detection range
-   Flexible operation modes: distance-only or distance + water depth
-   FMCW technology for reliable water level detection

## Hardware Setup

Connect your HLK-LD8001H sensor to your ESP32/ESP8266 using the following pins:

-   HLK-LD8001H 3.7V → ESP 5V
-   HLK-LD8001H GND → ESP GND
-   HLK-LD8001H TTL_TX → ESP RX (GPIO16 in example)
-   HLK-LD8001H TTL_RX → ESP TX (GPIO17 in example)

With the antenna facing away from you and the plug on the right side (top to bottom):

-   Pin 1 → 3.7V (3.5V - 5V)
-   Pin 2 → GND
-   Pin 3 → NC
-   Pin 4 → NC
-   Pin 5 → UART TX
-   Pin 6 → UART RX

## Configuration Variables

-   **uart_id** (_Required_, ID): The ID of the UART bus
-   **name** (_Required_, string): Name for the distance sensor
-   **installation_height** (_Optional_): Distance from sensor to tank bottom/ground (valid range: 0.15m to 40m). If not specified, water depth calculation will not be displayed.
-   **range** (_Optional_, distance, default: 10m): Maximum detection range (valid range: 0.15m to 40m)
-   **modbus_address** (_Optional_, hex, default: 0x01): MODBUS device address (valid range: 0x01 to 0xFD)
-   **water_depth_sensor** (_Optional_, ID): ID for the water depth sensor. Only used if installation_height is specified.
-   **update_interval** (_Required_, time): How often to poll the sensor and publish state updates

## Basic Configuration

### Example 1: With Installation Height (Distance + Water Depth)

```yaml
uart:
    id: uart_bus
    tx_pin: GPIO17
    rx_pin: GPIO16
    baud_rate: 115200

# Water level with depth calculation
sensor:
    # Distance to water surface
    - platform: hlk_ld8001h
      uart_id: uart_bus
      name: "Distance to Water"
      unit_of_measurement: "mm"
      update_interval: 2s
      installation_height: 2m # Distance from sensor to water empty level / tank bottom (0.15m - 40m)
      range: 3m # Default 10m, Max detection range (0.15m - 40m), ideally should be 0.5-1m greater than installation_height
      water_depth_sensor: water_depth_id

    # Water depth (calculated as installation height minus distance)
    - platform: template
      id: water_depth_id
      name: "Water Depth"
      unit_of_measurement: "mm"
      device_class: "distance"
      state_class: "measurement"
      accuracy_decimals: 1
```

### Example 2: Distance-Only Mode

```yaml
uart:
    id: uart_bus
    tx_pin: GPIO17
    rx_pin: GPIO16
    baud_rate: 115200

# Distance-only measurement
sensor:
    - platform: hlk_ld8001h
      uart_id: uart_bus
      name: "Distance to Target"
      unit_of_measurement: "mm"
      update_interval: 2s
      range: 3m # Default 10m, 0.15m-40m valid range, 0.5m-1m greater than expected max distance
```

## Installation

There are two ways to install this component:

### Option 1: Direct from GitHub

Add the following to your ESPHome configuration:

```yaml
external_components:
    - source: github://Averyy/esphome-custom-components
      components: [hlk_ld8001h]
```

### Option 2: Local Installation

If you prefer a more stable setup or need to modify the component:

1. Create a `components` directory in your ESPHome configuration directory
2. Create an `hlk_ld8001h` directory inside `components`
3. Copy the component files into that directory
4. Add the `external_components` section to your YAML configuration:

```yaml
external_components:
    - source: components
```

## Mounting Recommendations

For optimal performance:

-   Mount the sensor perpendicular to the liquid surface
-   Avoid installation above feed inlets
-   Maintain a minimum safe distance from walls
-   Avoid obstructions within the beam range

## Sensor Readings

The component provides two possible sensor readings:

1. **Distance to Target/Water (Space Height)**: Direct distance from the sensor to the target surface in mm (always available)
2. **Water Depth**: Calculated water level (installation height minus space height) in mm (only available if installation_height is specified)

## Operation Modes

### Distance-Only Mode

When `installation_height` is not specified, the component operates in distance-only mode. It will only report the direct distance from the sensor to the target surface. This is useful for general distance measurement applications or when installation height is not known.

### Water Level Mode

When `installation_height` is specified, the component operates in water level mode. It will report both the direct distance to the water surface and calculate the water depth. This is useful for water tank monitoring applications.

## Technical Specifications (HLK-LD8001H)

-   **Frequency**: 77-81 GHz (80 GHz nominal)
-   **Range**: 0.15-40m
-   **Measurement accuracy**: ±5mm
-   **Resolution**: 1mm
-   **Beamwidth**: ±25° standard, ±3° with external lens antenna
-   **Power supply**: 3.5-5V DC
-   **Current consumption**: Typically 30mA
-   **Operating temperature**: -45°C to +85°C
-   **Module dimensions**: 35 x 35 x 1.2 mm
-   **Weight**: 5g

## Troubleshooting

If you encounter issues:

-   Check all wiring connections
-   Verify the sensor has proper power (3.3V)
-   Check your UART pins are correctly connected (TX → RX, RX → TX)
-   Try power cycling the sensor
-   Review the ESPHome logs for error messages
-   Make sure installation height is set correctly (if using water depth calculation)
-   Verify nothing is obstructing the radar beam
-   If you see warnings about operations taking too long, try increasing the update interval

## Notes

-   The sensor uses FMCW (Frequency-Modulated Continuous Wave) radar technology
-   For best accuracy, ensure the sensor is securely mounted perpendicular to the water surface
-   After changing any configuration parameters, the sensor will be automatically reconfigured on boot
-   Optimal minimum range starts at 15cm - avoid using for measurements below this
-   The component includes retry mechanisms if communication fails

## Communications Protocol

### Register Specifications

| Register | Access     | Description         | Command Example                                                         | Notes                                                                                                     |
| -------- | ---------- | ------------------- | ----------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| 0x0001   | Read-only  | Space Height        | 01 03 00 01 00 01 D5 CA                                                 | Unit: mm, filtered data                                                                                   |
| 0x0003   | Read-only  | Water Level         | 01 03 00 03 00 01 74 0A                                                 | Unit: mm, filtered data                                                                                   |
| 0x0005   | Read/Write | Installation Height | Read: 01 03 00 05 00 01 94 0B<br>Write: 01 06 00 05 [AA] [BB] [CC] [DD] | Unit: cm. Distance from radar to water bottom. Water level equals installation height minus space height. |
| 0x3F4    | Read/Write | Device Address      | Read: 01 03 03 F4 00 01 C5 BC<br>Write: 01 06 03 F4 [AA] [BB] [CC] [DD] | Range: 0x01-0xFD. Supports broadcast 0xFF                                                                 |
| 0x3F6    | Read/Write | Baud Rate           | Read: 01 03 03 F6 00 01 64 7C<br>Write: 01 06 03 F6 [AA] [BB] [CC] [DD] | Values: 48, 96, 144, 192, 384, 560, 576, 1152, 1290<br>Read/write baud rate = actual baud rate/100        |
| 0x07D4   | Read/Write | Range               | Read: 01 03 07 D4 00 01 C5 46<br>Write: 01 06 07 D4 00 [AA] [BB] [CC]   | Default: 0x0A, unit: m. Maximum range depends on product model                                            |

### Communication Format

1. **Data Format**: 8 data bits, No parity, 1 stop bit (8N1)
2. **Default Settings**: Address = 1, Baud rate = 115200
3. **CRC Verification**: CRC16 (polynomial A001)

### Command Structure

-   **First byte**: Device address
-   **Second byte**: Function code (0x03 to read register values, 0x06 to write to registers)
-   **Third & fourth bytes**: Register address
-   **Fifth & sixth bytes**:
    -   For read operations: Number of registers to read
    -   For write operations: Data to store in register
-   **Last two bytes**: CRC checksum

### Example Command Breakdown

Example: `01 06 07 DB 00 05 38 86`

-   `01`: Device address (1)
-   `06`: Function code (write single register)
-   `07 DB`: Register address
-   `00 05`: Data to write
-   `38 86`: CRC checksum

### Notes

-   When writing to registers, parameters [AA] [BB] [CC] [DD] must be determined based on requirements
-   Register addresses in commands are in hexadecimal format
-   The water level is calculated as: Installation Height - Space Height
-   Need to set installation height first to get accurate water level readings
-   Default range is typically 10m (0x0A)
