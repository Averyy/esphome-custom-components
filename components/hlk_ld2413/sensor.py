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

CONF_MIN_DISTANCE = "min_distance"
CONF_MAX_DISTANCE = "max_distance"
CONF_REPORT_CYCLE = "report_cycle"

# Validation constants from datasheet
MIN_VALID_DISTANCE = 150  # mm
MAX_VALID_DISTANCE = 10500  # mm
MIN_REPORT_CYCLE = 50  # ms
MAX_REPORT_CYCLE = 1000  # ms

hlk_ld2413_ns = cg.esphome_ns.namespace('hlk_ld2413')
HLKLD2413Sensor = hlk_ld2413_ns.class_('HLKLD2413Sensor', sensor.Sensor, cg.PollingComponent)

def validate_config(config):
    if config[CONF_MIN_DISTANCE] >= config[CONF_MAX_DISTANCE]:
        raise cv.Invalid("min_distance must be less than max_distance")
    return config

# Create a modified UART schema that only requires baud_rate
# Start with the base UART schema
base_uart_schema = uart.UART_DEVICE_SCHEMA.schema.copy()

# Make data_bits, parity, and stop_bits optional with defaults
modified_uart_schema = {
    **base_uart_schema,
    cv.Optional(uart.CONF_DATA_BITS, default=8): cv.int_range(5, 8),
    cv.Optional(uart.CONF_PARITY, default="NONE"): cv.one_of("NONE", "EVEN", "ODD", upper=True),
    cv.Optional(uart.CONF_STOP_BITS, default=1): cv.one_of(1, 2),
}

# Create our custom UART schema
UART_SCHEMA = cv.Schema(modified_uart_schema)

CONFIG_SCHEMA = cv.All(
    sensor.sensor_schema(
        HLKLD2413Sensor,
        unit_of_measurement=UNIT_MILLIMETER,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_DISTANCE,
        state_class=STATE_CLASS_MEASUREMENT,
    ).extend({
        cv.GenerateID(): cv.declare_id(HLKLD2413Sensor),
        cv.Required(CONF_UPDATE_INTERVAL): cv.update_interval,
        cv.Optional(CONF_MIN_DISTANCE, default=f"{MIN_VALID_DISTANCE}mm"): cv.distance,
        cv.Optional(CONF_MAX_DISTANCE, default=f"{MAX_VALID_DISTANCE}mm"): cv.distance,
        cv.Optional(CONF_REPORT_CYCLE, default=f"160ms"): cv.positive_time_period_milliseconds,
    }).extend(UART_SCHEMA),
    validate_config
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await sensor.register_sensor(var, config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    
    if CONF_MIN_DISTANCE in config:
        # Convert from meters to millimeters
        dist_mm = int(config[CONF_MIN_DISTANCE] * 1000)
        cg.add(var.set_min_distance(dist_mm))
        
    if CONF_MAX_DISTANCE in config:
        # Convert from meters to millimeters
        dist_mm = int(config[CONF_MAX_DISTANCE] * 1000)
        cg.add(var.set_max_distance(dist_mm))
        
    if CONF_REPORT_CYCLE in config:
        # Extract milliseconds from TimePeriodMilliseconds object
        report_cycle_ms = int(config[CONF_REPORT_CYCLE].total_milliseconds)
        cg.add(var.set_report_cycle(report_cycle_ms))