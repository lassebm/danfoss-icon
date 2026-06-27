#pragma once
#include "danfoss_icon.h"
#include "esphome/core/component.h"
#include "esphome/components/number/number.h"

namespace esphome {
namespace danfoss_icon {

// Generic writable number bound to one (idx, attr_id), encoded as u16 BE ×100 °C (temperature) —
// e.g. the per-room setpoint min/max (0x0507/0x0508). HA edits call control(); the controller's
// read-back confirms via on_attr. The room's climate entity re-reads the same attr on poll, so its
// visual bounds + clamp follow an edit automatically (after the usual reconnect for visual bounds).
class DanfossIconNumber : public number::Number, public Component, public DanfossIconListener {
 public:
  void set_parent(DanfossIconHub *parent) { parent_ = parent; }
  void set_index(uint8_t idx) { idx_ = idx; }
  void set_attribute(uint16_t attr) { attr_ = attr; }

  void setup() override;
  void dump_config() override;
  void on_attr(uint8_t idx, uint16_t attr_id, const uint8_t *data, size_t len) override;

 protected:
  void control(float value) override;  // HA wrote a new value -> queue a controller write

  DanfossIconHub *parent_{nullptr};
  uint8_t idx_{0};
  uint16_t attr_{0};
};

}  // namespace danfoss_icon
}  // namespace esphome
