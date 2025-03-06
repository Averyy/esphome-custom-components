# ESPHome Custom Components

This repository contains custom components for [ESPHome](https://esphome.io/), a system to control ESP32 devices with simple YAML configuration files.

## Available Components

-   [HLK-LD2413](#hlk-ld2413) - 24GHz mmWave radar liquid level detection sensor
-   [Notecard](#notecard) - Blues Wireless Notecard IoT modules
-   [VL53L1X](#vl53l1x) - Time-of-Flight distance sensor

## HLK LD2413

A high-precision liquid level detection sensor using 24GHz millimeter wave radar technology with a detection range of 0.25m to 10.5m and accuracy of ±3mm under optimal conditions. Note that the datasheet says 150mm minimum range, but in reality it's 250mm.

This UART component provides support for this advanced mmWave distance sensing product. I developed it specifically for measuring water levels in an underground cistern where traditional ultrasonic sensors struggled with winter temperature variations, darkness, and humidity. The mmWave technology offers superior reliability in challenging environments.

### Installation

```yaml
external_components:
    - source: github://Averyy/esphome-custom-components
      components: [hlk_ld2413]
```

**Resources:**

-   [Documentation](components/hlk_ld2413/README.md)
-   [Datasheet](components/hlk_ld2413/datasheet.txt)
-   [Example Configuration](example_hlk_ld2413.yaml)
-   Purchase: [AliExpress Option 1](https://www.aliexpress.com/item/1005006766564668.html) / [AliExpress Option 2](https://www.aliexpress.com/item/1005008479449270.html)

## Blues Notecard

Integration with Blues Wireless Notecard (both cellular and WiFi variants) for IoT connectivity. Features include configurable data collection and sync intervals, automatic data batching, and access to Notecard temperature and battery voltage.

This UART component was developed to transmit magnetic dial gauge readings from a propane tank in a location without WiFi access. The implementation focuses specifically on using the Notecard for data transmission to Notehub, while the ESP32 handles sleep cycles and data filtering for optimal power efficiency.

### Installation

```yaml
external_components:
    - source: github://Averyy/esphome-custom-components
      components: [notecard]
```

**Resources:**

-   [Documentation](components/notecard/README.md)
-   [Example Configuration](example_notecard.yaml)
-   [Purchase: Blues Shop](https://shop.blues.com/collections/notecard)

## VL53L1X

Integration with the VL53L1X/VL53L4CD Time-of-Flight (ToF) distance sensor. Supports both VL53L1X and VL53L4CD sensors with configurable distance modes and range status reporting.

This I2C component was forked from an existing open source repository after updates caused compatibility issues with my setup. I created this stable version primarily for water level monitoring applications, though I discovered that lens condensation in humid environments could affect reading stability - an important consideration for similar implementations.

### Installation

```yaml
external_components:
    - source: github://Averyy/esphome-custom-components
      components: [vl53l1x]
```

**Resources:**

-   [Documentation](components/vl53l1x/README.md)
-   [Example Configuration](example_vl53l1x.yaml)

## General Information

### Features

-   **Easy Integration**: Simple YAML configuration for complex sensors
-   **Fully Documented**: Comprehensive documentation for each component
-   **Example Configurations**: Ready-to-use example files for quick setup

### Note on Installation

Using the GitHub repository version means you'll automatically get updates, but it could change at any moment. If stability is critical for your project, consider using the local installation method described in each component's documentation.

## License

This project is licensed under the MIT License with Attribution - see the [LICENSE](LICENSE) file for details.
