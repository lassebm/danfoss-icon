#include "sensor.h"
#include "esphome/core/log.h"
#include <cmath>

namespace esphome {
namespace danfoss_icon {

static const char *const TAG = "danfoss_icon.sensor";

void DanfossIconSensor::setup() {
  parent_->add_listener(this);
  if (idx_ >= 0x31)
    parent_->add_room(idx_);
  else if (idx_ >= 0x01 && idx_ <= 0x03)
    parent_->add_controller(idx_);
  parent_->add_slow_attr(idx_, attr_);  // poll this attr (slow tier); tier/dedup resolved at build
}

void DanfossIconSensor::on_attr(uint8_t idx, uint16_t attr_id, const uint8_t *data, size_t len) {
  if (idx != idx_ || attr_id != attr_)
    return;
  float val = NAN;
  switch (decode_) {
    case DI_DECODE_BATTERY:
      if (len >= 1) {
        uint8_t b = data[0];
        val = (b <= 100) ? (float) b : (b == 0xFE ? 0.0f : NAN);  // 0xFE = battery low
      }
      break;
    case DI_DECODE_RAW_U8:
      if (len >= 1)
        val = data[0];
      break;
    case DI_DECODE_TEMP:
      if (len >= 2) {
        uint16_t v = ((uint16_t) data[0] << 8) | data[1];
        val = (v == DI_TEMP_INVALID) ? NAN : v / 100.0f;
      }
      break;
    case DI_DECODE_U16:
      if (len >= 2)
        val = ((uint16_t) data[0] << 8) | data[1];
      break;
  }
  // Publish only on change. The framework re-emits on every publish_state() (DEBUG log + API
  // state update + HA recorder write), and fast-tier attrs poll ~every 2 s, so re-publishing an
  // unchanged value is pure churn/log-spam. Deduping is safe: ESPHome resends the stored state on
  // each API (re)connect, so HA stays in sync. (Same guard the binary sensors in status.h use.)
  if (this->has_state() && ((std::isnan(val) && std::isnan(this->state)) || val == this->state))
    return;
  this->publish_state(val);
}

void DanfossIconSensor::dump_config() {
  LOG_SENSOR("", "Danfoss Icon Sensor", this);
  ESP_LOGCONFIG(TAG, "  idx=0x%02X attr=0x%04X decode=%d", idx_, attr_, (int) decode_);
}

}  // namespace danfoss_icon
}  // namespace esphome
