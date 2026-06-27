#pragma once
#include "danfoss_icon.h"
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace danfoss_icon {

// How to turn a raw attribute value into a numeric sensor reading.
enum DiSensorDecode {
  DI_DECODE_BATTERY = 0,  // u8: 0..100 = %, 0xFE = low (->0), other >100 = unknown (NaN)
  DI_DECODE_RAW_U8 = 1,   // u8 as-is (enums/flags)
  DI_DECODE_TEMP = 2,     // u16 BE x100 -> degC, 0x8000 -> NaN
  DI_DECODE_U16 = 3,      // u16 BE as-is
};

// Generic numeric sensor bound to one (idx, attr_id).
class DanfossIconSensor : public sensor::Sensor, public Component, public DanfossIconListener {
 public:
  void set_parent(DanfossIconHub *parent) { parent_ = parent; }
  void set_index(uint8_t idx) { idx_ = idx; }
  void set_attribute(uint16_t attr) { attr_ = attr; }
  void set_decode(DiSensorDecode d) { decode_ = d; }

  void setup() override;
  void dump_config() override;
  void on_attr(uint8_t idx, uint16_t attr_id, const uint8_t *data, size_t len) override;

 protected:
  DanfossIconHub *parent_{nullptr};
  uint8_t idx_{0};
  uint16_t attr_{0};
  DiSensorDecode decode_{DI_DECODE_RAW_U8};
};

}  // namespace danfoss_icon
}  // namespace esphome
