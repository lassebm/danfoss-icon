#include "climate.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <cmath>

namespace esphome {
namespace danfoss_icon {

static const char *const TAG = "danfoss_icon.climate";

void DanfossIconClimate::setup() {
  this->parent_->add_listener(this);
  this->parent_->add_room(this->idx_);
  this->mode = climate::CLIMATE_MODE_HEAT;      // initial; corrected from 0x0509 on first poll
  this->preset = climate::CLIMATE_PRESET_HOME;  // initial; corrected from 0x100A on first poll
  // Persist the restore-on-HEAT setpoint across reboots: when a room is left OFF (setpoint parked
  // at frost), this is the value HEAT restores. Keyed off the entity's object-id hash.
  this->saved_target_pref_ = global_preferences->make_preference<float>(this->get_object_id_hash() ^ 0x53415645U);
  float v;
  if (this->saved_target_pref_.load(&v) && !std::isnan(v))
    this->saved_target_ = v;
  // Restore per-room setpoint bounds (0x0507/0x0508) so traits() reports the right visual range at
  // boot — the API entity-list runs before the first poll. Refreshed from the controller on poll.
  this->room_min_pref_ = global_preferences->make_preference<float>(this->get_object_id_hash() ^ 0x4D494E00U);
  this->room_max_pref_ = global_preferences->make_preference<float>(this->get_object_id_hash() ^ 0x4D415800U);
  if (this->room_min_pref_.load(&v) && !std::isnan(v))
    this->room_min_ = v;
  if (this->room_max_pref_.load(&v) && !std::isnan(v))
    this->room_max_ = v;
}

climate::ClimateTraits DanfossIconClimate::traits() {
  auto t = climate::ClimateTraits();
  t.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_ACTION);
  t.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT});
  // Native room modes: Home/Away/Sleep <-> 0x100A {0,1,2}.
  t.set_supported_presets({climate::CLIMATE_PRESET_HOME, climate::CLIMATE_PRESET_AWAY, climate::CLIMATE_PRESET_SLEEP});
  t.set_visual_min_temperature(this->room_min_);
  t.set_visual_max_temperature(this->room_max_);
  t.set_visual_target_temperature_step(0.5f);
  t.set_visual_current_temperature_step(0.1f);
  return t;
}

// Active preset attribute/value, selected by the current room mode (0x100A): Home/Away/Sleep.
uint16_t DanfossIconClimate::active_attr_() const {
  return this->room_mode_ == 1   ? DI_ATTR_SETPOINT_AWAY
         : this->room_mode_ == 2 ? DI_ATTR_SETPOINT_SLEEP
                                 : DI_ATTR_SETPOINT_HOME;
}

float DanfossIconClimate::active_sp_() const {
  return this->room_mode_ == 1 ? this->sp_away_ : this->room_mode_ == 2 ? this->sp_asleep_ : this->sp_home_;
}

void DanfossIconClimate::write_setpoint_(float t) {
  uint16_t raw =
      (uint16_t) lroundf(clamp(t, this->room_min_, this->room_max_) * 100.0f);  // preset setpoint = u16 BE x100
  uint8_t v[2] = {(uint8_t) (raw >> 8), (uint8_t) (raw & 0xFF)};
  this->parent_->queue_write(this->idx_, this->active_attr_(), v, 2);
}

// Reflect the active preset's stored value into the HA target + HEAT/OFF mode (OFF == at/below frost).
void DanfossIconClimate::apply_active_setpoint_(float t) {
  if (std::isnan(t))
    return;  // that preset not polled yet — leave target/mode until it arrives
  this->target_temperature = t;
  if (!this->at_frost_(t)) {
    this->mode = climate::CLIMATE_MODE_HEAT;
    if (this->room_mode_ == 0)
      this->set_saved_target_(t);  // only Home drives the OFF/HEAT restore value
  } else {
    this->mode = climate::CLIMATE_MODE_OFF;
  }
  this->publish_if_changed_();
}

// Decode a preset-setpoint reply (u16 BE ×100, 0x8000 = invalid) into `slot`; if that preset is the
// active one, reflect it into the HA target/mode. Shared by the 0x0509/0x050A/0x050B on_attr cases.
void DanfossIconClimate::on_preset_setpoint_(uint16_t attr, float &slot, const uint8_t *data, size_t len) {
  if (len < 2)
    return;
  uint16_t v = ((uint16_t) data[0] << 8) | data[1];
  if (v == DI_TEMP_INVALID)
    return;
  slot = v / 100.0f;
  if (this->active_attr_() == attr)
    this->apply_active_setpoint_(slot);
}

// Map room_mode_ (0x100A) onto the HA preset.
void DanfossIconClimate::set_room_mode_(uint8_t m) {
  this->room_mode_ = m;
  this->preset = m == 1   ? climate::CLIMATE_PRESET_AWAY
                 : m == 2 ? climate::CLIMATE_PRESET_SLEEP
                          : climate::CLIMATE_PRESET_HOME;
}

// Snap the active preset back to Home (0x100A=0) so OFF/HEAT always act on 0x0509 and never park the
// Away/Sleep configured values at frost. No-op when already Home.
void DanfossIconClimate::reset_to_home_() {
  if (this->room_mode_ == 0)
    return;
  const uint8_t home = 0x00;
  this->parent_->queue_write(this->idx_, DI_ATTR_ROOM_MODE, &home, 1);
  this->set_room_mode_(0);
}

void DanfossIconClimate::set_saved_target_(float t) {
  if (std::isnan(t) || t == this->saved_target_)
    return;  // persist only real changes — avoids needless flash writes on every 0x0509 poll
  this->saved_target_ = t;
  this->saved_target_pref_.save(&this->saved_target_);
}

void DanfossIconClimate::set_limit_(float &slot, ESPPreferenceObject &pref, float v) {
  if (std::isnan(v) || v == slot)
    return;  // persist only real changes — bounds rarely move, avoid flash wear on every slow poll
  slot = v;
  pref.save(&slot);
  // Visual bounds are part of the entity descriptor (sent at API connect), so HA shows the new
  // range only after a reconnect; the live clamp in write_setpoint_/control already uses it.
  this->publish_state();
}

// Preset select: switch the controller's room mode (0x100A) and regulate to that preset's setpoint.
// The hub keeps every room manual (0x100B==0) so the firmware applies 0x100A directly. If a prior
// OFF parked Home at frost, choosing Home restores the saved heat setpoint (so Home un-does OFF, per
// room or via All Rooms Mode). Away/Sleep are never parked by OFF, so selecting them just reflects
// their stored value — no setpoint write is issued on mere selection.
void DanfossIconClimate::set_preset_mode(uint8_t m) {
  const uint8_t mb = m;
  this->parent_->queue_write(this->idx_, DI_ATTR_ROOM_MODE, &mb, 1);
  this->set_room_mode_(m);
  if (m == 0 && !std::isnan(this->saved_target_) && this->at_frost_(this->sp_home_)) {
    this->write_setpoint_(this->saved_target_);  // un-park Home (active is Home now) -> turns the room back on
    this->target_temperature = this->saved_target_;
    this->mode = climate::CLIMATE_MODE_HEAT;
  } else {
    this->apply_active_setpoint_(this->active_sp_());  // reflect the stored preset value (if polled); mode from it
  }
  this->publish_if_changed_();  // publish the preset change even if the setpoint hasn't been polled yet
  ESP_LOGD(TAG, "room 0x%02X preset -> %u", this->idx_, m);
}

// The controller has no native "off" — a room is turned off by parking Home (0x0509) at frost/min
// (5 °C). OFF/HEAT always snap the preset back to Home so Away/Sleep values are never frozen.
void DanfossIconClimate::set_off() {
  this->reset_to_home_();
  float save = !std::isnan(this->sp_home_) ? this->sp_home_ : this->target_temperature;
  if (!std::isnan(save) && !this->at_frost_(save))
    this->set_saved_target_(save);  // remember (persisted) to restore on HEAT
  this->write_setpoint_(this->room_min_);
  this->sp_home_ = this->room_min_;  // reflect the park now so an immediate Home-select un-parks
  this->mode = climate::CLIMATE_MODE_OFF;
  this->target_temperature = this->room_min_;  // optimistic; confirmed on next 0x0509 poll
  this->publish_if_changed_();
  ESP_LOGD(TAG, "room 0x%02X OFF (setpoint -> frost %.1f C)", this->idx_, this->room_min_);
}

void DanfossIconClimate::set_heat() {
  this->reset_to_home_();
  float t = !std::isnan(this->saved_target_)
                ? this->saved_target_
                : (!this->at_frost_(this->target_temperature) ? this->target_temperature : 20.0f);
  this->write_setpoint_(t);
  this->mode = climate::CLIMATE_MODE_HEAT;
  this->target_temperature = t;
  this->publish_if_changed_();
  ESP_LOGD(TAG, "room 0x%02X HEAT (setpoint -> %.2f C)", this->idx_, t);
}

void DanfossIconClimate::control(const climate::ClimateCall &call) {
  if (call.get_preset().has_value()) {
    climate::ClimatePreset p = *call.get_preset();
    this->set_preset_mode(p == climate::CLIMATE_PRESET_AWAY ? 1 : p == climate::CLIMATE_PRESET_SLEEP ? 2 : 0);
  }
  if (call.get_mode().has_value()) {
    climate::ClimateMode m = *call.get_mode();
    if (m == climate::CLIMATE_MODE_OFF)
      this->set_off();
    else if (m == climate::CLIMATE_MODE_HEAT)
      this->set_heat();
  }
  if (call.get_target_temperature().has_value()) {
    float t = clamp(*call.get_target_temperature(), this->room_min_, this->room_max_);
    this->write_setpoint_(t);      // edits the active preset's setpoint (Home/Away/Sleep per the room mode)
    this->target_temperature = t;  // optimistic; confirmed on next poll
    this->mode = this->at_frost_(t) ? climate::CLIMATE_MODE_OFF : climate::CLIMATE_MODE_HEAT;
    this->publish_if_changed_();
    ESP_LOGD(TAG, "room 0x%02X set target %.2f C", this->idx_, t);
  }
}

void DanfossIconClimate::on_attr(uint8_t idx, uint16_t attr_id, const uint8_t *data, size_t len) {
  if (idx != this->idx_)
    return;
  switch (attr_id) {
    case 0x0300: {  // room temperature
      if (len < 2)
        break;
      uint16_t v = ((uint16_t) data[0] << 8) | data[1];
      this->room_temp_ = (v == DI_TEMP_INVALID) ? NAN : v / 100.0f;
      this->update_current_temp_();
      break;
    }
    case 0x0304: {  // floor temperature (current_temp source when regulating by floor sensor)
      if (len < 2)
        break;
      uint16_t v = ((uint16_t) data[0] << 8) | data[1];
      this->floor_temp_ = (v == DI_TEMP_INVALID) ? NAN : v / 100.0f;
      this->update_current_temp_();
      break;
    }
    case 0x030A: {  // floor-sensor mode: 0=Comfort (air), 1=Floor, 2=Dual (air + floor clamp)
      if (len < 1)
        break;
      this->floor_mode_ = data[0];
      this->update_current_temp_();
      break;
    }
    // Home/Away/Sleep preset setpoints. Each is stored; only the active one drives target/mode.
    case DI_ATTR_SETPOINT_HOME:
      this->on_preset_setpoint_(DI_ATTR_SETPOINT_HOME, this->sp_home_, data, len);
      break;
    case DI_ATTR_SETPOINT_AWAY:
      this->on_preset_setpoint_(DI_ATTR_SETPOINT_AWAY, this->sp_away_, data, len);
      break;
    case DI_ATTR_SETPOINT_SLEEP:
      this->on_preset_setpoint_(DI_ATTR_SETPOINT_SLEEP, this->sp_asleep_, data, len);
      break;
    case DI_ATTR_ROOM_MODE: {  // room mode: 0=Home 1=Away 2=Sleep (the active preset)
      if (len < 1)
        break;
      this->set_room_mode_(data[0] <= 2 ? data[0] : 0);
      this->apply_active_setpoint_(this->active_sp_());
      this->publish_if_changed_();  // publish the preset even if that setpoint hasn't been polled yet
      break;
    }
    case 0x0507: {  // per-room setpoint lower bound (menu-configurable)
      if (len < 2)
        break;
      uint16_t v = ((uint16_t) data[0] << 8) | data[1];
      if (v != DI_TEMP_INVALID)
        this->set_limit_(this->room_min_, this->room_min_pref_, v / 100.0f);
      break;
    }
    case 0x0508: {  // per-room setpoint upper bound (menu-configurable)
      if (len < 2)
        break;
      uint16_t v = ((uint16_t) data[0] << 8) | data[1];
      if (v != DI_TEMP_INVALID)
        this->set_limit_(this->room_max_, this->room_max_pref_, v / 100.0f);
      break;
    }
    case 0x1013: {  // heat/cool state: 0=off 1=heat 2=cool
      if (len < 1)
        break;
      this->action = data[0] == 1   ? climate::CLIMATE_ACTION_HEATING
                     : data[0] == 2 ? climate::CLIMATE_ACTION_COOLING
                                    : climate::CLIMATE_ACTION_IDLE;
      this->publish_if_changed_();
      break;
    }
    default:
      break;
  }
}

void DanfossIconClimate::update_current_temp_() {
  // Current temp follows the regulated sensor: Floor mode (0x030A==1) shows floor temp (0x0304);
  // Comfort (0) and Dual (2) show room/air temp (0x0300) — Dual regulates air, the floor is only
  // clamped. Matches the Danfoss app and thermostat user guide.
  float ct = (this->floor_mode_ == 1) ? this->floor_temp_ : this->room_temp_;
  if (std::isnan(ct))
    return;  // chosen sensor not reported yet (e.g. floor mode before a valid floor reading)
  this->current_temperature = ct;
  this->publish_if_changed_();
}

// Publish only when a HA-visible field (mode/action/target/current) actually moved — avoids fast-poll
// re-emit churn (see sensor.cpp). Visual bounds (0x0507/0x0508) ride a reconnect via traits(), so
// they're published separately in set_limit_.
void DanfossIconClimate::publish_if_changed_() {
  auto same = [](float a, float b) { return (std::isnan(a) && std::isnan(b)) || a == b; };
  if (this->published_ && this->mode == this->last_mode_ && this->action == this->last_action_ &&
      this->preset == this->last_preset_ && same(this->target_temperature, this->last_target_) &&
      same(this->current_temperature, this->last_current_))
    return;
  this->last_mode_ = this->mode;
  this->last_action_ = this->action;
  this->last_preset_ = this->preset;
  this->last_target_ = this->target_temperature;
  this->last_current_ = this->current_temperature;
  this->published_ = true;
  this->publish_state();
}

void DanfossIconClimate::dump_config() {
  LOG_CLIMATE("", "Danfoss Icon Climate", this);
  ESP_LOGCONFIG(TAG, "  Room index: 0x%02X", this->idx_);
}

}  // namespace danfoss_icon
}  // namespace esphome
