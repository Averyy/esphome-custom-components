import esphome.codegen as cg # type: ignore
import esphome.config_validation as cv # type: ignore
from esphome.components import sensor, uart # type: ignore
from esphome.const import ( # type: ignore
    CONF_ID,
    CONF_UPDATE_INTERVAL,
    DEVICE_CLASS_DISTANCE,
    STATE_CLASS_MEASUREMENT,
    UNIT_MILLIMETER,
)

DEPENDENCIES = ['uart']
AUTO_LOAD = ['sensor']

CONF_INSTALLATION_HEIGHT = "installation_height"
CONF_RANGE = "range"
CONF_MODBUS_ADDRESS = "modbus_address"
CONF_WATER_DEPTH_SENSOR = "water_depth_sensor"

# Validation constants from datasheet
MIN_VALID_DISTANCE = 150  # mm
MAX_VALID_DISTANCE = 40000  # mm
DEFAULT_RANGE = 10  # m (10m)

hlk_ld8001h_ns = cg.esphome_ns.namespace('hlk_ld8001h')
HLKLD8001HSensor = hlk_ld8001h_ns.class_('HLKLD8001HSensor', sensor.Sensor, cg.PollingComponent)

def validate_config(config):
    # Validate installation_height is within range if provided
    if CONF_INSTALLATION_HEIGHT in config:
        installation_height_mm = int(config[CONF_INSTALLATION_HEIGHT] * 1000)  # Convert from meters to millimeters
        if installation_height_mm < MIN_VALID_DISTANCE:
            raise cv.Invalid(f"installation_height must be at least {MIN_VALID_DISTANCE}mm (got {installation_height_mm}mm)")
        if installation_height_mm > MAX_VALID_DISTANCE:
            raise cv.Invalid(f"installation_height must be at most {MAX_VALID_DISTANCE}mm (got {installation_height_mm}mm)")
    
    # Validate range is within bounds
    range_mm = int(config[CONF_RANGE] * 1000)  # Convert from meters to millimeters
    if range_mm < MIN_VALID_DISTANCE:
        raise cv.Invalid(f"range must be at least {MIN_VALID_DISTANCE}mm (got {range_mm}mm)")
    if range_mm > MAX_VALID_DISTANCE:
        raise cv.Invalid(f"range must be at most {MAX_VALID_DISTANCE}mm (got {range_mm}mm)")
    
    # Validate modbus address
    if CONF_MODBUS_ADDRESS in config:
        modbus_address = config[CONF_MODBUS_ADDRESS]
        if isinstance(modbus_address, int) and (modbus_address < 0x01 or modbus_address > 0xFD):
            raise cv.Invalid(f"modbus_address must be between 0x01 and 0xFD (got {hex(modbus_address)})")
    
    return config

CONFIG_SCHEMA = cv.All(
    sensor.sensor_schema(
        HLKLD8001HSensor,
        unit_of_measurement=UNIT_MILLIMETER,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_DISTANCE,
        state_class=STATE_CLASS_MEASUREMENT,
    ).extend({
        cv.GenerateID(): cv.declare_id(HLKLD8001HSensor),
        cv.Required(CONF_UPDATE_INTERVAL): cv.update_interval,
        cv.Required(uart.CONF_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Optional(CONF_INSTALLATION_HEIGHT): cv.distance,
        cv.Optional(CONF_RANGE, default=f"{DEFAULT_RANGE}m"): cv.distance,
        cv.Optional(CONF_MODBUS_ADDRESS, default=0x01): cv.hex_uint8_t,
        cv.Optional(CONF_WATER_DEPTH_SENSOR): cv.use_id(sensor.Sensor),
    }),
    validate_config
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await sensor.register_sensor(var, config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    
    if CONF_INSTALLATION_HEIGHT in config:
        # Convert from meters to centimeters (as per datasheet)
        height_cm = int(config[CONF_INSTALLATION_HEIGHT] * 100)  # Convert to cm
        cg.add(var.set_installation_height(height_cm))
        cg.add(var.set_has_installation_height(True))
    else:
        cg.add(var.set_has_installation_height(False))
        
    if CONF_RANGE in config:
        # Convert from meters to meters (as per datasheet)
        range_m = int(config[CONF_RANGE])  # Convert to whole meters
        cg.add(var.set_range(range_m))
        
    if CONF_MODBUS_ADDRESS in config:
        cg.add(var.set_modbus_address(config[CONF_MODBUS_ADDRESS]))
        
    if CONF_WATER_DEPTH_SENSOR in config:
        water_depth_sensor = await cg.get_variable(config[CONF_WATER_DEPTH_SENSOR])
        cg.add(var.set_water_depth_sensor(water_depth_sensor))