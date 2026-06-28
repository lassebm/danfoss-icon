#pragma once
#include "danfoss_icon.h"
#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/core/preferences.h"
#include <cmath>

namespace esphome {
namespace danfoss_icon {

// One HA climate entity per room. Modes HEAT/OFF — the controller has no native off, so OFF parks
// the active setpoint (0x0509) at the room's frost/min (0x0507) and HEAT restores it. The visual
// range + clamp track the per-room setpoint bounds (0x0507/0x0508). current_temperature
// follows the regulation sensor (0x030A: room 0x0300, or floor 0x0304 in floor mode); action from
// heat/cool state (0x1013).
class DanfossIconClimate : public climate::Climate, public Component, public DanfossIconListener {
 public:
  void set_parent(DanfossIconHub *parent) { parent_ = parent; }
  void set_room_index(uint8_t idx) { idx_ = idx; }

  void setup() override;
  void dump_config() override;
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;
  void on_attr(uint8_t idx, uint16_t attr_id, const uint8_t *data, size_t len) override;

 protected:
  // current_temperature follows the room's regulation sensor (0x030A): floor temp (0x0304) for
  // floor-only rooms (mode 1), room temp (0x0300) for room (0) and dual (2) — dual regulates air
  // with the floor merely clamped.
  void update_current_temp_();
  void publish_if_changed_();       // publish only when mode/action/target/current actually moved
  void write_setpoint_(float t);    // queue a 0x0509 write (BE centi-°C), clamped to range
  void set_saved_target_(float t);  // update + persist (NVS) the restore-on-HEAT setpoint
  void set_limit_(float &slot, ESPPreferenceObject &pref, float v);  // update + persist a min/max bound

  DanfossIconHub *parent_{nullptr};
  uint8_t idx_{0};
  uint8_t floor_mode_{0};    // 0x030A floor-sensor mode: 0=Comfort, 1=Floor, 2=Dual
  float room_temp_{NAN};     // 0x0300
  float floor_temp_{NAN};    // 0x0304
  float saved_target_{NAN};  // last heat setpoint, restored when leaving OFF (persisted to flash)
  // Per-room setpoint bounds from 0x0507/0x0508 (menu-configurable). Persisted so traits() returns
  // the right visual range at boot (API entity-list runs before the first poll); defaults are the
  // controller's frost/max. HA only re-reads visual bounds on reconnect, but the clamp is live.
  float room_min_{5.0f};                   // 0x0507 per-room setpoint lower limit
  float room_max_{35.0f};                  // 0x0508 per-room setpoint upper limit
  ESPPreferenceObject saved_target_pref_;  // NVS persistence for saved_target_ (survives reboot)
  ESPPreferenceObject room_min_pref_;      // NVS persistence for room_min_
  ESPPreferenceObject room_max_pref_;      // NVS persistence for room_max_
  // Last-published snapshot, compared by publish_if_changed_().
  climate::ClimateMode last_mode_{};
  climate::ClimateAction last_action_{};
  float last_target_{NAN};
  float last_current_{NAN};
  bool published_{false};
};

}  // namespace danfoss_icon
}  // namespace esphome
