import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome import pins
from esphome.const import CONF_ID

DEPENDENCIES = ["uart", "network"]
modbus_tcp_gateway_ns = cg.esphome_ns.namespace("modbus_tcp_gateway")
ModbusTcpGateway = modbus_tcp_gateway_ns.class_("ModbusTcpGateway", cg.Component, uart.UARTDevice)
CONF_DIRECTION_PIN = "direction_pin"
CONF_PORT = "port"
CONF_TIMEOUT = "response_timeout"
CONF_TURNAROUND = "turnaround_delay"
CONF_ALLOW_WRITES = "allow_writes"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ModbusTcpGateway),
    cv.Required(CONF_DIRECTION_PIN): pins.gpio_output_pin_schema,
    cv.Optional(CONF_PORT, default=5020): cv.port,
    cv.Optional(CONF_TIMEOUT, default="1000ms"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_TURNAROUND, default="200us"): cv.positive_time_period_microseconds,
    cv.Optional(CONF_ALLOW_WRITES, default=False): cv.boolean,
}).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    pin = await cg.gpio_pin_expression(config[CONF_DIRECTION_PIN])
    cg.add(var.set_direction_pin(pin))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_response_timeout(config[CONF_TIMEOUT].total_milliseconds))
    cg.add(var.set_turnaround_us(config[CONF_TURNAROUND].total_microseconds))
    cg.add(var.set_allow_writes(config[CONF_ALLOW_WRITES]))
