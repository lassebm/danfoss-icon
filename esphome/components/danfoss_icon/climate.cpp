#include "climate.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <cmath>

namespace esphome {
namespace danfoss_icon {

static const char *const TAG = "danfoss_icon.climate";

void DanfossIconClimate::setup() {
  parent_->add_listener(this);
  parent_->add_room(idx_);
  this->mode = climate::CLIMATE_MODE_HEAT;  // initial; corrected from 0x0509 on first poll
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
  t.set_visual_min_temperature(room_min_);
  t.set_visual_max_temperature(room_max_);
  t.set_visual_target_temperature_step(0.5f);
  t.set_visual_current_temperature_step(0.1f);
  return t;
}

void DanfossIconClimate::write_setpoint_(float t) {
  uint16_t raw = (uint16_t) lroundf(clamp(t, room_min_, room_max_) * 100.0f);  // 0x0509 = u16 BE x100
  uint8_t v[2] = {(uint8_t) (raw >> 8), (uint8_t) (raw & 0xFF)};
  parent_->queue_write(idx_, 0x0509, v, 2);
}

void DanfossIconClimate::set_saved_target_(float t) {
  if (std::isnan(t) || t == saved_target_)
    return;  // persist only real changes — avoids needless flash writes on every 0x0509 poll
  saved_target_ = t;
  this->saved_target_pref_.save(&saved_target_);
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

void DanfossIconClimate::control(const climate::ClimateCall &call) {
  // The controller has no native "off" — a room is turned off by parking its active setpoint at
  // frost/min (5 °C). So HEAT/OFF map to a real vs frost 0x0509; room control stays manual.
  if (call.get_mode().has_value()) {
    climate::ClimateMode m = *call.get_mode();
    if (m == climate::CLIMATE_MODE_OFF) {
      if (this->target_temperature > room_min_ + 0.05f)
        set_saved_target_(this->target_temperature);  // remember (persisted) to restore on HEAT
      write_setpoint_(room_min_);
      this->mode = climate::CLIMATE_MODE_OFF;
      this->target_temperature = room_min_;  // optimistic; confirmed on next 0x0509 poll
      this->publish_if_changed_();
      ESP_LOGD(TAG, "room 0x%02X OFF (setpoint -> frost %.1f C)", idx_, room_min_);
    } else if (m == climate::CLIMATE_MODE_HEAT) {
      float t = !std::isnan(saved_target_)
                    ? saved_target_
                    : (this->target_temperature > room_min_ + 0.05f ? this->target_temperature : 20.0f);
      write_setpoint_(t);
      this->mode = climate::CLIMATE_MODE_HEAT;
      this->target_temperature = t;
      this->publish_if_changed_();
      ESP_LOGD(TAG, "room 0x%02X HEAT (setpoint -> %.2f C)", idx_, t);
    }
  }
  if (call.get_target_temperature().has_value()) {
    float t = clamp(*call.get_target_temperature(), room_min_, room_max_);
    write_setpoint_(t);
    this->target_temperature = t;  // optimistic; confirmed on next poll of 0x0509
    this->mode = (t <= room_min_ + 0.05f) ? climate::CLIMATE_MODE_OFF : climate::CLIMATE_MODE_HEAT;
    this->publish_if_changed_();
    ESP_LOGD(TAG, "room 0x%02X set target %.2f C", idx_, t);
  }
}

void DanfossIconClimate::on_attr(uint8_t idx, uint16_t attr_id, const uint8_t *data, size_t len) {
  if (idx != idx_)
    return;
  switch (attr_id) {
    case 0x0300: {  // room temperature
      if (len < 2)
        break;
      uint16_t v = ((uint16_t) data[0] << 8) | data[1];
      room_temp_ = (v == DI_TEMP_INVALID) ? NAN : v / 100.0f;
      update_current_temp_();
      break;
    }
    case 0x0304: {  // floor temperature (current_temp source when regulating by floor sensor)
      if (len < 2)
        break;
      uint16_t v = ((uint16_t) data[0] << 8) | data[1];
      floor_temp_ = (v == DI_TEMP_INVALID) ? NAN : v / 100.0f;
      update_current_temp_();
      break;
    }
    case 0x030A: {  // floor-sensor mode: 0=Comfort (air), 1=Floor, 2=Dual (air + floor clamp)
      if (len < 1)
        break;
      floor_mode_ = data[0];
      update_current_temp_();
      break;
    }
    case 0x0509: {  // active setpoint — also drives HEAT/OFF (OFF == parked at frost/min)
      if (len < 2)
        break;
      uint16_t v = ((uint16_t) data[0] << 8) | data[1];
      if (v == DI_TEMP_INVALID)
        break;
      float t = v / 100.0f;
      this->target_temperature = t;
      if (t > room_min_ + 0.05f) {
        this->mode = climate::CLIMATE_MODE_HEAT;
        set_saved_target_(t);  // track the live heat setpoint (persisted) so OFF→HEAT restores it
      } else {
        this->mode = climate::CLIMATE_MODE_OFF;  // at/below frost/min = effectively off
      }
      this->publish_if_changed_();
      break;
    }
    case 0x0507: {  // per-room setpoint lower bound (menu-configurable)
      if (len < 2)
        break;
      uint16_t v = ((uint16_t) data[0] << 8) | data[1];
      if (v != DI_TEMP_INVALID)
        set_limit_(room_min_, room_min_pref_, v / 100.0f);
      break;
    }
    case 0x0508: {  // per-room setpoint upper bound (menu-configurable)
      if (len < 2)
        break;
      uint16_t v = ((uint16_t) data[0] << 8) | data[1];
      if (v != DI_TEMP_INVALID)
        set_limit_(room_max_, room_max_pref_, v / 100.0f);
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
  float ct = (floor_mode_ == 1) ? floor_temp_ : room_temp_;
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
  if (published_ && this->mode == last_mode_ && this->action == last_action_ &&
      same(this->target_temperature, last_target_) && same(this->current_temperature, last_current_))
    return;
  last_mode_ = this->mode;
  last_action_ = this->action;
  last_target_ = this->target_temperature;
  last_current_ = this->current_temperature;
  published_ = true;
  this->publish_state();
}

void DanfossIconClimate::dump_config() {
  LOG_CLIMATE("", "Danfoss Icon Climate", this);
  ESP_LOGCONFIG(TAG, "  Room index: 0x%02X", idx_);
}

}  // namespace danfoss_icon
}  // namespace esphome
