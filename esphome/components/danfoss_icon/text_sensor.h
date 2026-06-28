#pragma once
#include "danfoss_icon.h"
#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace danfoss_icon {

enum DiTextDecode {
  DI_TEXT_VERSTR = 0,       // length-prefixed ASCII (e.g. 0x007F device fw "4.38")
  DI_TEXT_REVISION = 1,     // u16 -> "major.minor" from the two bytes (e.g. 0x0214 -> "2.20")
  DI_TEXT_HEX = 2,          // raw bytes as lowercase hex
  DI_TEXT_PRODUCT = 3,      // 0x0080 descriptor -> product name (id at bytes [4:5])
  DI_TEXT_OUTPUTS = 4,      // union of 0x1020/21/22 (SLOW/MEDIUM/FAST) -> 1-indexed actuator channels
  DI_TEXT_HWVER = 5,        // 0x0015 REVISION bytes [0:1] -> "maj.min" (hardware version)
  DI_TEXT_SWVER = 6,        // 0x0015 REVISION bytes [2:3] -> "maj.min" (software version)
  DI_TEXT_SERIAL = 7,       // u32 BE -> decimal integer string
  DI_TEXT_FAULT_ROOM = 8,   // 0x03F0 ROOM error code bitmask -> human fault list ("OK" if clear)
  DI_TEXT_FAULT_RAIL = 9,   // 0x03F0 RAIL/SYSTEM error code bitmask -> human fault list (incl outputs)
  DI_TEXT_FLOOR_MODE = 10,  // 0x030A floor-sensor mode -> "Comfort" / "Floor" / "Dual"
};

// Generic text sensor bound to one (idx, attr_id) — for identity / diagnostics.
class DanfossIconTextSensor : public text_sensor::TextSensor, public Component, public DanfossIconListener {
 public:
  void set_parent(DanfossIconHub *parent) { parent_ = parent; }
  void set_index(uint8_t idx) { idx_ = idx; }
  void set_attribute(uint16_t attr) { attr_ = attr; }
  void set_decode(DiTextDecode d) { decode_ = d; }

  void setup() override;
  void dump_config() override;
  void on_attr(uint8_t idx, uint16_t attr_id, const uint8_t *data, size_t len) override;

 protected:
  DanfossIconHub *parent_{nullptr};
  uint8_t idx_{0};
  uint16_t attr_{0};
  DiTextDecode decode_{DI_TEXT_HEX};
  uint16_t out_slow_{0}, out_med_{0}, out_fast_{0};  // cached output-group bitmaps (DI_TEXT_OUTPUTS)
};

}  // namespace danfoss_icon
}  // namespace esphome
