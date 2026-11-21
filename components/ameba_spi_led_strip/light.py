from dataclasses import dataclass

from esphome import pins
import esphome.codegen as cg
from esphome.components import libretiny, light
from esphome.components.const import CONF_CHANNEL_COLORS
import esphome.config_validation as cv
from esphome.const import (
    CONF_CHIPSET,
    CONF_MAX_REFRESH_RATE,
    CONF_NUM_LEDS,
    CONF_PIN,
    CONF_OUTPUT_ID
)

CODEOWNERS = ["@pulleralarm"]
DEPENDENCIES = ["libretiny"]

ameba_spi_led_strip_ns = cg.esphome_ns.namespace("ameba_spi_led_strip")
AmebaSPILEDStripLightOutput = ameba_spi_led_strip_ns.class_(
    "AmebaSPILEDStripLightOutput", light.AddressableLight
)

@dataclass
class LEDStripTimings:
    bit0: int
    bit1: int
    spi_frequency: int


CHIPSETS = {
    "WS2812": LEDStripTimings(
        0b11100000, 0b11111100, 6400000
    ),  # Clock divider: 9, Bit time: 1350ns
    "SK6812": LEDStripTimings(
        0b11000000, 0b11111000, 7500000
    ),  # Clock divider: 8, Bit time: 1200ns
}


CONF_DUMMY_MISO_PIN = "dummy_miso"
CONF_DUMMY_SCLK_PIN = "dummy_sclk"
CONF_DUMMY_CS_PIN = "dummy_cs"

SUPPORTED_MOSI = {
    "name" : "MOSI (output)",
    libretiny.const.FAMILY_RTL8720C : [ 4, 9, 19 ]
}
SUPPORTED_MISO = {
    "name" : "MISO (dummy)",
    libretiny.const.FAMILY_RTL8720C : [ 10, 20 ]
}
SUPPORTED_SCLK = {
    "name" : "SLCK (dummy)",
    libretiny.const.FAMILY_RTL8720C : [ 3, 8 ] # Cannot disable PA16 as UART TX?
}
SUPPORTED_CS = {
    "name" : "CS (dummy)",
    libretiny.const.FAMILY_RTL8720C : [ 2, 7, 15 ] # PA15 will disable UART RX
}


def _validate_pin(pindef, value):
    family = libretiny.get_libretiny_family()
    if family not in pindef:
        raise cv.Invalid(f"Chip family {family} is not supported.")
    if value not in pindef[family]:
        supported_pin_info = ", ".join(f"{x}" for x in pindef[family])
        pinname = pindef["name"]
        raise cv.Invalid(
            f"Pin {value} for {pinname} is not supported on the {family}. Supported pins: {supported_pin_info}"
        )
    return value

def _validate_MOSI(value):
    return _validate_pin(SUPPORTED_MOSI, value)

def _validate_MISO(value):
    return _validate_pin(SUPPORTED_MISO, value)

def _validate_SCLK(value):
    return _validate_pin(SUPPORTED_SCLK, value)

def _validate_CS(value):
    return _validate_pin(SUPPORTED_CS, value)


def _validate_num_leds(value):
    # A white channel makes each LED one byte wider, so fewer of them fit in the DMA buffer.
    max_num_leds = 123 if "W" in value[CONF_CHANNEL_COLORS] else 165  # 127 / 170
    if value[CONF_NUM_LEDS] > max_num_leds:
        raise cv.Invalid(
            f"The maximum number of LEDs for this configuration is {max_num_leds}.",
            path=CONF_NUM_LEDS,
        )
    return value


CONFIG_SCHEMA = cv.All(
    light.ADDRESSABLE_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(AmebaSPILEDStripLightOutput),
            cv.Required(CONF_PIN): cv.All(
                pins.internal_gpio_output_pin_number, _validate_MOSI
            ),
            cv.Required(CONF_DUMMY_MISO_PIN): cv.All(
                pins.internal_gpio_output_pin_number, _validate_MISO
            ),
            cv.Required(CONF_DUMMY_SCLK_PIN): cv.All(
                pins.internal_gpio_output_pin_number, _validate_SCLK
            ),
            cv.Required(CONF_DUMMY_CS_PIN): cv.All(
                pins.internal_gpio_output_pin_number, _validate_CS
            ),
            cv.Required(CONF_NUM_LEDS): cv.positive_not_null_int,
            cv.Optional(CONF_CHANNEL_COLORS): light.validate_channel_colors,
            cv.Optional(CONF_MAX_REFRESH_RATE): cv.positive_time_period_microseconds,
            cv.Required(CONF_CHIPSET): cv.one_of(*CHIPSETS, upper=True),
        }
    ),
    _validate_num_leds,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await light.register_light(var, config)
    await cg.register_component(var, config)

    cg.add(var.set_num_leds(config[CONF_NUM_LEDS]))
    cg.add(var.set_pins(config[CONF_PIN], config[CONF_DUMMY_MISO_PIN], config[CONF_DUMMY_SCLK_PIN], config[CONF_DUMMY_CS_PIN]))

    if CONF_MAX_REFRESH_RATE in config:
        cg.add(var.set_max_refresh_rate(config[CONF_MAX_REFRESH_RATE]))

    chipset = CHIPSETS[config[CONF_CHIPSET]]
    cg.add(
        var.set_led_params(
            chipset.bit0,
            chipset.bit1,
            chipset.spi_frequency,
        )
    )

    cg.add(var.set_channel_colors(light.channel_colors_struct(config[CONF_CHANNEL_COLORS])))

    num_leds = config[CONF_NUM_LEDS]
    channel_colors = config[CONF_CHANNEL_COLORS]
    led_data = light.static_led_buffer(
        config[CONF_OUTPUT_ID], num_leds, len(channel_colors)
    )
    dma_buf = cg.static_buffer(
        config[CONF_OUTPUT_ID],
        cg.uint8,
        ameba_spi_led_strip_ns.dma_buffer_bytes(num_leds * len(channel_colors)),
        align=4,
    )
    cg.add(var.set_buffers(led_data, dma_buf))

