# ESPHome Custom Components

This repository contains custom components for [ESPHome](https://esphome.io/), a system to control ESP32 devices with simple YAML configuration files.

## Available Components

- [HLK-LD2413](#hlk-ld2413) - 24GHz mmWave radar liquid level detection sensor
- [Notecard](#notecard) - Blues Wireless Notecard IoT modules
- [VL53L1X](#vl53l1x) - Time-of-Flight distance sensor

## HLK-LD2413

A high-precision liquid level detection sensor using 24GHz millimeter wave radar technology with a detection range of 0.15m to 10.5m and accuracy of ±3mm under optimal conditions.

### Installation

```yaml
external_components:
  - source: github://Averyy/esphome-custom-components
    components: [hlk_ld2413]
```

**Resources:**

- [Documentation](components/hlk_ld2413/README.md)
- [Datasheet](components/hlk_ld2413/datasheet.txt)
- [Example Configuration](example_hlk_ld2413.yaml)
- [Purchase: AliExpress Option 1](https://www.aliexpress.com/item/1005006766564668.html) / [AliExpress Option 2](https://www.aliexpress.com/item/1005008479449270.html)

## Notecard

Integration with Blues Wireless Notecard (both cellular and WiFi variants) for IoT connectivity. Features include configurable data collection and sync intervals, automatic data batching, and access to Notecard temperature and battery voltage.

### Installation

```yaml
external_components:
  - source: github://Averyy/esphome-custom-components
    components: [notecard]
```

**Resources:**

- [Documentation](components/notecard/README.md)
- [Example Configuration](example_notecard.yaml)
- [Purchase: Blues Shop](https://shop.blues.com/collections/notecard)

## VL53L1X

Integration with the VL53L1X/VL53L4CD Time-of-Flight (ToF) distance sensor. Supports both VL53L1X and VL53L4CD sensors with configurable distance modes and range status reporting.

Note this component was forked from an open source repo where updates caused it to break for me, so I copied it to make my own stable version.

### Installation

```yaml
external_components:
  - source: github://Averyy/esphome-custom-components
    components: [vl53l1x]
```

**Resources:**

- [Documentation](components/vl53l1x/README.md)
- [Example Configuration](example_vl53l1x.yaml)

## General Information

### Features

- **Easy Integration**: Simple YAML configuration for complex sensors
- **Fully Documented**: Comprehensive documentation for each component
- **Example Configurations**: Ready-to-use example files for quick setup

### Note on Installation

Using the GitHub repository version means you'll automatically get updates, but it could change at any moment. If stability is critical for your project, consider using the local installation method described in each component's documentation.

## License

This project is licensed under the MIT License with Attribution - see the [LICENSE](LICENSE) file for details.
