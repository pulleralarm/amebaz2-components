#include "led_strip.h"

#ifdef USE_LIBRETINY_VARIANT_RTL8720C

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

/* Hack to avoid type collission between realtek spi and arduino */
#define PinMode PinModeRTL

extern "C" {
#include "spi_api.h"
#include "spi_ex_api.h"
#include "sys_api.h"

/* Hack to avoid linker error */
void DBG_SSI_ERR() {
}

/* Not defined in any header file */
extern void log_uart_port_init (int log_uart_tx, int log_uart_rx, uint32_t baud_rate);

};

namespace esphome {
namespace ameba_spi_led_strip {

static const char *const TAG = "ameba_spi_led_strip";

static volatile int spi_dma_running;

static void spi_dma_tx_finish_callback(void *pdata, SpiIrq event) {
  spi_dma_running = 0;
}

void AmebaSPILEDStripLightOutput::setup() {
  size_t buffer_size = this->get_buffer_size_();
  size_t dma_buffer_size = (buffer_size * 8) + (2 * 64);
  spi_t *spi_master = reinterpret_cast<spi_t *>(malloc(sizeof(spi_t)));
  memset(spi_master, 0, sizeof(spi_t));

  // pin_, dummy_miso_, dummy_sclk_, dummy_cs_
  // Not sure if this is needed
  if (dummy_sclk_ == PA_2 || dummy_cs_ == PA_3) {
    sys_jtag_off();
  }
  // But for sure we will have to disable UART RX when PA15 is selected as CS
  if (dummy_cs_ == PA_15) {
    sys_log_uart_off();
    log_uart_port_init(PA_16, 255, 115200);
  }
  this->spi_master = spi_master;
  spi_init(spi_master, static_cast<PinName>(pin_), static_cast<PinName>(dummy_miso_),
           static_cast<PinName>(dummy_sclk_), static_cast<PinName>(dummy_cs_));
  spi_format(spi_master, DfsEightBits, ((int)SPI_SCLK_IDLE_LOW|(int)SPI_SCLK_TOGGLE_MIDDLE), 0);
  spi_frequency(spi_master, spi_frequency_);

  RAMAllocator<uint8_t> allocator;
  this->buf_ = allocator.allocate(buffer_size);
  if (this->buf_ == nullptr) {
    ESP_LOGE(TAG, "Cannot allocate LED buffer!");
    this->mark_failed();
    return;
  }

  this->effect_data_ = allocator.allocate(this->num_leds_);
  if (this->effect_data_ == nullptr) {
    ESP_LOGE(TAG, "Cannot allocate effect data!");
    this->mark_failed();
    return;
  }

  this->dma_buf_ = allocator.allocate(dma_buffer_size);
  if (this->dma_buf_ == nullptr) {
    ESP_LOGE(TAG, "Cannot allocate DMA buffer!");
    this->mark_failed();
    return;
  }

  memset(this->buf_, 0, buffer_size);
  memset(this->effect_data_, 0, this->num_leds_);
  memset(this->dma_buf_, 0, dma_buffer_size);

}

void AmebaSPILEDStripLightOutput::set_led_params(uint8_t bit0, uint8_t bit1, uint32_t spi_frequency) {
  this->bit0_ = bit0;
  this->bit1_ = bit1;
  this->spi_frequency_ = spi_frequency;
}

void AmebaSPILEDStripLightOutput::write_state(light::LightState *state) {
  // protect from refreshing too often
  uint32_t now = micros();
  if (*this->max_refresh_rate_ != 0 && (now - this->last_refresh_) < *this->max_refresh_rate_) {
    // try again next loop iteration, so that this change won't get lost
    this->schedule_show();
    return;
  }
  if (spi_dma_running) {
    ESP_LOGE(TAG, "DMA running during write_state");
    return;
  }

  this->last_refresh_ = now;
  this->mark_shown_();

  if (spi_master == nullptr) {
    ESP_LOGE(TAG, "SPI not initialized");
    this->status_set_warning();
    return;
  }

  size_t buffer_size = this->get_buffer_size_();
  size_t size = 0;
  uint8_t *psrc = this->buf_;
  uint8_t *pdest = this->dma_buf_ + 64;
  // The 64 byte padding is a workaround for a SPI DMA bug where the
  // output doesn't exactly start at the beginning of dma_buf_

  while (size < buffer_size) {
    uint8_t b = *psrc;
    for (int i = 0; i < 8; i++) {
      *pdest++ = b & (1 << (7 - i)) ? this->bit1_ : this->bit0_;
    }
    size++;
    psrc++;
  }

  spi_irq_hook(reinterpret_cast<spi_t *>(spi_master), (spi_irq_handler)spi_dma_tx_finish_callback, (uint32_t)this);
  spi_dma_running = 1;
  spi_master_write_stream_dma(reinterpret_cast<spi_t *>(spi_master), (char *)(this->dma_buf_+64), buffer_size*8 + 16);

  this->status_clear_warning();
}

light::ESPColorView AmebaSPILEDStripLightOutput::get_view_internal(int32_t index) const {
  int32_t r = 0, g = 0, b = 0;
  switch (this->rgb_order_) {
    case ORDER_RGB:
      r = 0;
      g = 1;
      b = 2;
      break;
    case ORDER_RBG:
      r = 0;
      g = 2;
      b = 1;
      break;
    case ORDER_GRB:
      r = 1;
      g = 0;
      b = 2;
      break;
    case ORDER_GBR:
      r = 2;
      g = 0;
      b = 1;
      break;
    case ORDER_BGR:
      r = 2;
      g = 1;
      b = 0;
      break;
    case ORDER_BRG:
      r = 1;
      g = 2;
      b = 0;
      break;
  }
  uint8_t multiplier = this->is_rgbw_ || this->is_wrgb_ ? 4 : 3;
  uint8_t white = this->is_wrgb_ ? 0 : 3;

  return {this->buf_ + (index * multiplier) + r + this->is_wrgb_,
          this->buf_ + (index * multiplier) + g + this->is_wrgb_,
          this->buf_ + (index * multiplier) + b + this->is_wrgb_,
          this->is_rgbw_ || this->is_wrgb_ ? this->buf_ + (index * multiplier) + white : nullptr,
          &this->effect_data_[index],
          &this->correction_};
}

void AmebaSPILEDStripLightOutput::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Ameba SPI LED Strip:\n"
                "  Pin: %u, Dummy_MISO: %u, Dummy_SCLK: %u, Dummy_CS: %u\n",
                pin_, dummy_miso_, dummy_sclk_, dummy_cs_);
  const char *rgb_order;
  switch (this->rgb_order_) {
    case ORDER_RGB:
      rgb_order = "RGB";
      break;
    case ORDER_RBG:
      rgb_order = "RBG";
      break;
    case ORDER_GRB:
      rgb_order = "GRB";
      break;
    case ORDER_GBR:
      rgb_order = "GBR";
      break;
    case ORDER_BGR:
      rgb_order = "BGR";
      break;
    case ORDER_BRG:
      rgb_order = "BRG";
      break;
    default:
      rgb_order = "UNKNOWN";
      break;
  }
  ESP_LOGCONFIG(TAG,
                "  RGB Order: %s\n"
                "  Max refresh rate: %" PRIu32 "\n"
                "  Number of LEDs: %u",
                rgb_order, *this->max_refresh_rate_, this->num_leds_);
}

float AmebaSPILEDStripLightOutput::get_setup_priority() const { return setup_priority::HARDWARE; }

}  // namespace ameba_spi_led_strip
}  // namespace esphome

#endif  // USE_LIBRETINY_VARIANT_RTL8720C
