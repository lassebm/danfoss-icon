#include "number.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <cmath>

namespace esphome {
namespace danfoss_icon {

static const char *const TAG = "danfoss_icon.number";

void DanfossIconNumber::setup() {
  parent_->add_listener(this);
  if (idx_ >= 0x31)
    parent_->add_room(idx_);
  else if (idx_ >= 0x01 && idx_ <= 0x03)
    parent_->add_controller(idx_);
  parent_->add_slow_attr(idx_, attr_);  // poll this attr (slow tier); tier/dedup resolved at build
}

void DanfossIconNumber::on_attr(uint8_t idx, uint16_t attr_id, const uint8_t *data, size_t len) {
  if (idx != idx_ || attr_id != attr_ || len < 2)
    return;
  uint16_t v = ((uint16_t) data[0] << 8) | data[1];
  if (v == DI_TEMP_INVALID)
    return;
  float nv = v / 100.0f;
  if (this->has_state() && nv == this->state)
    return;  // publish only on change — fast-tier polls would otherwise re-emit every cycle (see sensor.cpp)
  this->publish_state(nv);
}

void DanfossIconNumber::control(float value) {
  uint16_t raw = (uint16_t) lroundf(value * 100.0f);  // u16 BE ×100 °C, same encoding as 0x0509
  uint8_t v[2] = {(uint8_t) (raw >> 8), (uint8_t) (raw & 0xFF)};
  parent_->queue_write(idx_, attr_, v, 2);
  this->publish_state(value);  // optimistic; confirmed (or corrected) on the next poll
  ESP_LOGD(TAG, "write idx=0x%02X attr=0x%04X -> %.2f C", idx_, attr_, value);
}

void DanfossIconNumber::dump_config() {
  LOG_NUMBER("", "Danfoss Icon Number", this);
  ESP_LOGCONFIG(TAG, "  idx=0x%02X attr=0x%04X", idx_, attr_);
}

}  // namespace danfoss_icon
}  // namespace esphome
