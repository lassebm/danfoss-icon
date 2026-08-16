#pragma once
#include "danfoss_icon.h"
#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/preferences.h"
#include <cmath>

namespace esphome {
namespace danfoss_icon {

// One HA climate entity per room. Modes HEAT/OFF — the controller has no native off, so OFF parks
// the active setpoint (0x0509) at the room's frost/min (0x0507) and HEAT restores it. The visual
// range + clamp track the per-room setpoint bounds (0x0507/0x0508). current_temperature
// follows the regulation sensor (0x030A: room 0x0300, or floor 0x0304 in floor mode); action from
// heat/cool state (0x1013).
//
// Presets: the controller's native room modes are exposed as HA preset_modes Home/Away/Sleep <->
// room mode 0x100A {0,1,2}, backed by the three preset setpoints 0x0509 (home) / 0x050A (away) /
// 0x050B (sleep). The active preset's setpoint is the HA target; the slider edits that preset's
// value. OFF/HEAT always act on Home (0x0509) and snap the preset back to Home, so they never park
// the Away/Sleep setpoints at frost; and selecting the Home preset restores the saved heat setpoint
// if a prior OFF left Home parked at frost, so choosing Home (per room or via All Rooms Mode) turns
// the room back on. The firmware only applies 0x100A when the room is manual (0x100B==0), which the
// hub keeps enforced for every room.
class DanfossIconClimate final : public climate::Climate, public Component, public DanfossIconListener {
 public:
  void set_parent(DanfossIconHub *parent) { this->parent_ = parent; }
  void set_room_index(uint8_t idx) { this->idx_ = idx; }
  // Optional mirror of target_temperature as a sensor entity (room `setpoint:`), republished from
  // publish_if_changed_() so the two can't drift.
  void set_setpoint_sensor(sensor::Sensor *s) { this->setpoint_sensor_ = s; }

  void setup() override;
  void dump_config() override;
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;
  void on_attr(uint8_t idx, uint16_t attr_id, const uint8_t *data, size_t len) override;

  // Mode setters shared by control() and the hub-level "All Rooms Mode" select. active_mode() reports
  // the room's current mode for the select's aggregate: 0=Home, 1=Away, 2=Sleep, 3=Off (parked at frost).
  void set_preset_mode(uint8_t m);
  void set_off();
  void set_heat();
  int active_mode() const { return this->mode == climate::CLIMATE_MODE_OFF ? 3 : this->room_mode_; }

 protected:
  // current_temperature follows the room's regulation sensor (0x030A): floor temp (0x0304) for
  // floor-only rooms (mode 1), room temp (0x0300) for room (0) and dual (2) — dual regulates air
  // with the floor merely clamped.
  void update_current_temp_();
  void publish_if_changed_();       // publish only when mode/action/target/current/preset moved
  void write_setpoint_(float t);    // queue a write of the active preset setpoint (BE centi-°C), clamped
  void set_saved_target_(float t);  // update + persist (NVS) the restore-on-HEAT setpoint
  void set_limit_(float &slot, ESPPreferenceObject &pref, float v);  // update + persist a min/max bound
  // At/below the room's frost floor (0x0507) == effectively off. Small hysteresis avoids float jitter.
  bool at_frost_(float t) const { return t <= this->room_min_ + 0.05f; }
  // Presets: the attribute / stored value of the currently active room mode (0x100A).
  uint16_t active_attr_() const;         // 0x0509 (Home) / 0x050A (Away) / 0x050B (Sleep)
  float active_sp_() const;              // stored value of the active preset (NAN until first poll)
  void apply_active_setpoint_(float t);  // reflect the active preset value into target/mode + publish
  void set_room_mode_(uint8_t m);        // update room_mode_ + this->preset
  void reset_to_home_();                 // snap active preset back to Home (write 0x100A=0 if needed)
  // Decode a preset setpoint reply (BE ×100, invalid-sentinel) into `slot`; apply if it's active.
  void on_preset_setpoint_(uint16_t attr, float &slot, const uint8_t *data, size_t len);

  DanfossIconHub *parent_{nullptr};
  uint8_t idx_{0};
  sensor::Sensor *setpoint_sensor_{nullptr};  // optional target_temperature mirror (room `setpoint:`)
  // Mode + the three preset setpoints (the target side). saved_target_ is the persisted Home value
  // restored when leaving OFF.
  uint8_t room_mode_{0};     // 0x100A room mode: 0=Home, 1=Away, 2=Sleep (the active preset)
  float sp_home_{NAN};       // 0x0509 home setpoint (also the OFF/HEAT setpoint)
  float sp_away_{NAN};       // 0x050A away setpoint
  float sp_asleep_{NAN};     // 0x050B sleep setpoint
  float saved_target_{NAN};  // last Home heat setpoint, restored when leaving OFF (persisted to flash)
  // Regulation-sensor state — drives current_temperature.
  uint8_t floor_mode_{0};  // 0x030A floor-sensor mode: 0=Comfort, 1=Floor, 2=Dual
  float room_temp_{NAN};   // 0x0300
  float floor_temp_{NAN};  // 0x0304
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
  optional<climate::ClimatePreset> last_preset_{};
  float last_target_{NAN};
  float last_current_{NAN};
  bool published_{false};
};

}  // namespace danfoss_icon
}  // namespace esphome
