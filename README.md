# ESPHome Custom Components

This repository contains custom components for [ESPHome](https://esphome.io/), a system to control ESP32 devices with simple YAML configuration files.

## Components

| Component      | Description                                                 | Purchase                                                                                                                                                    | Origin                                    |
| -------------- | ----------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------- |
| **hlk_ld2413** | HLK-LD2413 24GHz mmWave radar liquid level detection sensor | [AliExpress Option 1](https://www.aliexpress.com/item/1005006766564668.html) / [AliExpress Option 2](https://www.aliexpress.com/item/1005008479449270.html) | Custom component created by me            |
| **notecard**   | Blues Wireless Notecard IoT modules                         | [Blues Shop](https://shop.blues.com/collections/notecard)                                                                                                   | Custom component created by me            |
| **vl53l1x**    | TOF400C-VL53L1X Time-of-Flight distance sensor              | -                                                                                                                                                           | Forked from an open source implementation |

## Installation

You can use these components in your ESPHome configuration by adding:

```yaml
external_components:
  - source: github://Averyy/esphome-custom-components
    components: [hlk_ld2413, notecard, vl53l1x]
```

To use a specific component only:

```yaml
external_components:
  - source: github://Averyy/esphome-custom-components
    components: [hlk_ld2413]
```

## Documentation

Each component has its own README:

- [HLK-LD2413 Documentation](components/hlk_ld2413/README.md) and [datasheet](components/hlk_ld2413/datasheet.txt)
- [Notecard Documentation](components/notecard/README.md)
- [VL53L1X Documentation](components/vl53l1x/README.md)

## Examples

Example YAML configurations are provided for each component:

- [HLK-LD2413 Example](example_hlk_ld2413.yaml)
- [Notecard Example](example_notecard.yaml)
- [VL53L1X Example](example_vl53l1x.yaml)

## Features

- **Easy Integration**: Simple YAML configuration for complex sensors
- **Fully Documented**: Comprehensive documentation for each component
- **Example Configurations**: Ready-to-use example files for quick setup

## License

This project is licensed under the MIT License with Attribution - see the [LICENSE](LICENSE) file for details.
