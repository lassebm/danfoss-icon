#pragma once
#include "danfoss_icon.h"
#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome {
namespace danfoss_icon {

// Problem rollup over a 0x03F0 error code (room or controller): device_class "problem", on when a
// real fault bit is set — the HA-visible alarm (area problem counts, automations). The Fault text
// sensor (DI_TEXT_FAULT_ROOM/RAIL) gives the detail of *which* fault.
//
// We AND 0x03F0 with a class-scoped mask rather than testing value != 0: the high-byte bits differ
// per class, and two bits must not alarm — the 0x0001 "errors present" summary, and (ROOMs only)
// 0x0200, which the RT raises on low battery (already shown on the Battery sensor). On the RAIL
// class 0x0200 means "Missing Radio module" and is kept. Per-class masks below.
class DanfossIconProblem : public binary_sensor::BinarySensor, public Component, public DanfossIconListener {
 public:
  void set_parent(DanfossIconHub *parent) { parent_ = parent; }
  void set_index(uint8_t idx) { idx_ = idx; }

  void setup() override {
    parent_->add_listener(this);
    if (idx_ >= 0x31)
      parent_->add_room(idx_);
    else if (idx_ >= 0x01 && idx_ <= 0x03)
      parent_->add_controller(idx_);
  }

  void on_attr(uint8_t idx, uint16_t attr_id, const uint8_t *data, size_t len) override {
    if (idx != idx_ || attr_id != 0x03F0 || len < 2)
      return;
    uint16_t v = (uint16_t(data[0]) << 8) | data[1];  // 0x03F0 error code, u16 BE
    // ROOM: Missing RT (0x0100) | floor short (0x0400) | floor disconnect (0x0800) — excludes the
    //   0x0200 RT-touch/low-battery bit and the 0x0001 summary bit.
    // RAIL/SYSTEM: all high-byte faults (0xFF00, incl. 0x0200 = Missing Radio module) + the output
    //   error bit (0x0002) — excludes only the 0x0001 summary bit.
    uint16_t mask = (idx_ >= 0x31) ? 0x0D00u : 0xFF02u;
    bool state = (v & mask) != 0;  // a real fault is present
    if (!published_ || state != last_state_) {
      this->publish_state(state);
      last_state_ = state;
      published_ = true;
    }
  }

 protected:
  DanfossIconHub *parent_{nullptr};
  uint8_t idx_{0};
  bool last_state_{false};
  bool published_{false};
};

}  // namespace danfoss_icon
}  // namespace esphome
