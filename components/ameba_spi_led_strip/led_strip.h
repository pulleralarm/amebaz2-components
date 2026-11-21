#pragma once

#ifdef USE_LIBRETINY_VARIANT_RTL8720C

#include "esphome/components/light/addressable_light.h"
#include "esphome/components/light/channel_colors.h"
#include "esphome/components/light/light_output.h"
#include "esphome/core/color.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace ameba_spi_led_strip {

/// 8 SPI bytes per color byte plus 2 * 64 bytes of padding
static constexpr size_t dma_buffer_bytes(size_t color_bytes) { return color_bytes * 8 + 2 * 64; }

class AmebaSPILEDStripLightOutput : public light::AddressableLight {
 public:
  void setup() override;
  void write_state(light::LightState *state) override;
  float get_setup_priority() const override;

  int32_t size() const override { return this->num_leds_; }
  light::LightTraits get_traits() override { return this->internal_accessor_.channel_colors.traits(); }

  void set_pins(uint8_t pin, uint8_t dummy_miso, uint8_t dummy_sclk, uint8_t dummy_cs) {
    this->pin_ = pin; this->dummy_miso_ = dummy_miso;
    this->dummy_sclk_ = dummy_sclk; this->dummy_cs_ = dummy_cs;
  }
  void set_num_leds(uint16_t num_leds) { this->num_leds_ = num_leds; }
  void set_channel_colors(light::ChannelColors channel_colors) {
    this->internal_accessor_.channel_colors = channel_colors;
  }

  /// Call after set_num_leds() and set_channel_colors(); `dma_buf` is a word aligned dma_buffer_bytes() block.
  void set_buffers(uint8_t *led_data, uint8_t *dma_buf) {
    this->internal_accessor_.set_buffer(led_data, this->num_leds_);
    this->dma_buf_ = dma_buf;
  }

  /// Set a maximum refresh rate in µs as some lights do not like being updated too often.
  void set_max_refresh_rate(uint32_t interval_us) { this->max_refresh_rate_ = interval_us; }

  void set_led_params(uint8_t bit0, uint8_t bit1, uint32_t spi_frequency);

  void clear_effect_data() override { this->internal_accessor_.clear_effect_data(this->num_leds_); }

  void dump_config() override;

 protected:
  void *spi_master;

  size_t get_buffer_size_() const { return this->num_leds_ * this->internal_accessor_.channel_colors.bytes_per_led(); }

  light::CorrectedBufferColorAccessor internal_accessor_{nullptr, nullptr, {0, 1, 2}, &this->correction_};
  uint8_t *dma_buf_{nullptr};

  uint8_t pin_, dummy_miso_, dummy_sclk_, dummy_cs_;

  uint16_t num_leds_;

  uint32_t spi_frequency_{6400000};
  uint8_t bit0_{0xE0};
  uint8_t bit1_{0xFC};

  uint32_t last_refresh_{0};
  optional<uint32_t> max_refresh_rate_{};
};

}  // namespace ameba_spi_led_strip
}  // namespace esphome

#endif  // USE_LIBRETINY_VARIANT_RTL8720C
