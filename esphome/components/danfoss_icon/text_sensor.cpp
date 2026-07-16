#include "text_sensor.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <cstdio>

namespace esphome {
namespace danfoss_icon {

static const char *const TAG = "danfoss_icon.text_sensor";

void DanfossIconTextSensor::setup() {
  this->parent_->add_listener(this);
  if (this->idx_ >= 0x31)
    this->parent_->add_room(this->idx_);
  else if (this->idx_ >= 0x01 && this->idx_ <= 0x03)
    this->parent_->add_controller(this->idx_);
  this->parent_->add_slow_attr(this->idx_, this->attr_);  // poll this attr (slow tier); tier/dedup resolved at build
}

void DanfossIconTextSensor::on_attr(uint8_t idx, uint16_t attr_id, const uint8_t *data, size_t len) {
  if (idx != this->idx_)
    return;
  // OUTPUTS spans three per-controller speed-category bitmaps (0x1020 SLOW / 0x1021 MEDIUM /
  // 0x1022 FAST). A room's outputs = the union; bit k = that controller's output k+1.
  if (this->decode_ == DI_TEXT_OUTPUTS) {
    if (len < 2)
      return;
    uint16_t bm = ((uint16_t) data[0] << 8) | data[1];
    if (attr_id == 0x1020)
      this->out_slow_ = bm;
    else if (attr_id == 0x1021)
      this->out_med_ = bm;
    else if (attr_id == 0x1022)
      this->out_fast_ = bm;
    else
      return;
    uint16_t all = this->out_slow_ | this->out_med_ | this->out_fast_;
    // "#"-prefix each channel so a single value reads as a channel id, not a count (e.g. "#2" =
    // output channel 2, never "2 outputs").
    std::string list = format_output_channels(all);
    if (!this->has_state() || this->state != list)  // publish only on change (see sensor.cpp)
      this->publish_state(list);
    return;
  }
  if (attr_id != this->attr_)
    return;
  std::string out;
  switch (this->decode_) {
    case DI_TEXT_VERSTR: {
      // [len][chars...][..]; chars are ASCII up to the prefix length or a NUL.
      if (len >= 1) {
        size_t n = data[0];
        if (n > len - 1)
          n = len - 1;
        for (size_t i = 0; i < n && data[1 + i] != 0; i++)
          out.push_back((char) data[1 + i]);
      }
      break;
    }
    case DI_TEXT_REVISION:
      if (len >= 2) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u.%02u", data[0], data[1]);
        out = buf;
      }
      break;
    case DI_TEXT_HEX:
      out = format_hex(data, len);  // lowercase hex string
      break;
    case DI_TEXT_PRODUCT:  // device descriptor 0x0080: product id at bytes [4:5]
      if (len >= 6) {
        uint16_t pid = ((uint16_t) data[4] << 8) | data[5];
        const char *name = product_id_name(pid);
        out = name != nullptr ? std::string(name) : str_sprintf("unknown(0x%04X)", pid);
      }
      break;
    case DI_TEXT_HWVER:  // 0x0015 REVISION bytes [0:1]
      if (len >= 2)
        out = str_sprintf("%u.%02u", data[0], data[1]);
      break;
    case DI_TEXT_SWVER:  // 0x0015 REVISION bytes [2:3]
      if (len >= 4)
        out = str_sprintf("%u.%02u", data[2], data[3]);
      break;
    case DI_TEXT_SERIAL:  // u32 BE as decimal
      if (len >= 4) {
        uint32_t v = ((uint32_t) data[0] << 24) | ((uint32_t) data[1] << 16) | ((uint32_t) data[2] << 8) | data[3];
        out = str_sprintf("%u", (unsigned) v);
      }
      break;
    case DI_TEXT_FAULT_ROOM:
    case DI_TEXT_FAULT_RAIL: {
      // 0x03F0 error code bitmask -> comma-joined human fault list ("OK" when clear). Bits follow
      // the controller's room/system status fault codes. Low bit 0x0001 is just the
      // "errors present" summary, so it is ignored here.
      if (len < 2)
        return;
      uint16_t v = ((uint16_t) data[0] << 8) | data[1];
      static const struct {
        uint16_t mask;
        const char *name;
      } ROOM_BITS[] = {
          {0x0100, "Thermostat missing"},
          {0x0200, "RT touch error"},
          {0x0400, "Floor sensor short circuit"},
          {0x0800, "Floor sensor disconnected"},
      };
      static const struct {
        uint16_t mask;
        const char *name;
      } RAIL_BITS[] = {
          {0x0100, "Expansion board missing"},        {0x0200, "Radio module missing"},
          {0x0400, "Command module missing"},         {0x0800, "Primary controller missing"},
          {0x1000, "Secondary controller 1 missing"}, {0x2000, "Secondary controller 2 missing"},
          {0x4000, "Pt1000 short circuit"},           {0x8000, "Pt1000 open circuit"},
      };
      if (this->decode_ == DI_TEXT_FAULT_ROOM) {
        for (auto &b : ROOM_BITS)
          if (v & b.mask)
            out += (out.empty() ? "" : ", ") + std::string(b.name);
      } else {
        // RAIL/SYSTEM faults set their high-byte bit AND the 0x0001 summary bit (expansion = 0x0101,
        // radio = 0x0201, … Pt1000-open = 0x8001), so test for both. That avoids matching the output
        // fault — the lone low-byte 0x0102 that does NOT set 0x0001 — which is reported separately below.
        for (auto &b : RAIL_BITS)
          if ((v & (b.mask | 0x0001u)) == (b.mask | 0x0001u))
            out += (out.empty() ? "" : ", ") + std::string(b.name);
        if (v & 0x0002)
          out += (out.empty() ? "" : ", ") + std::string("Output error");
      }
      if (out.empty())
        out = "OK";
      break;
    }
    case DI_TEXT_FLOOR_MODE:  // 0x030A floor-sensor mode (1 byte)
      if (len >= 1)
        out = data[0] == 0   ? "Comfort"  // room+floor: regulate air, floor min holds comfort
              : data[0] == 1 ? "Floor"    // regulate on floor sensor; setpoint is the floor target
              : data[0] == 2 ? "Dual"     // radiator regulates air, floor min/max clamp the floor
                             : str_sprintf("unknown(%u)", data[0]);
      break;
    case DI_TEXT_OUTPUTS:
      break;  // handled above (multi-attr union)
  }
  // A blank firmware string = the device hasn't reported 0x007F yet; hold the last known value
  // rather than clobbering it. For ROOM firmware this can persist: the controller keeps it in RAM
  // only and refreshes it solely on the thermostat's RF announce, so after a controller reboot a
  // room reads blank until that thermostat re-announces (a battery reinsert triggers it).
  if (this->decode_ == DI_TEXT_VERSTR && out.empty())
    return;
  if (this->has_state() && this->state == out)
    return;  // publish only on change (see sensor.cpp) — slow-tier strings rarely move
  this->publish_state(out);
}

void DanfossIconTextSensor::dump_config() {
  LOG_TEXT_SENSOR("", "Danfoss Icon Text Sensor", this);
  ESP_LOGCONFIG(TAG, "  idx=0x%02X attr=0x%04X decode=%d", this->idx_, this->attr_, (int) this->decode_);
}

}  // namespace danfoss_icon
}  // namespace esphome
