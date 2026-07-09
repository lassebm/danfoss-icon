#include "select.h"
#include "esphome/core/log.h"

namespace esphome {
namespace danfoss_icon {

static const char *const TAG = "danfoss_icon.select";

// Index = the mode code active_mode()/set_preset_mode() use: 0=Home, 1=Away, 2=Sleep, 3=Off.
static const char *const MODE_NAMES[] = {"Home", "Away", "Sleep", "Off"};
static const char *const MIXED = "Mixed";

void DanfossIconModeSelect::control(const std::string &value) {
  int sel = -1;
  for (int i = 0; i < 4; i++)
    if (value == MODE_NAMES[i])
      sel = i;
  if (sel < 0)
    return;  // "Mixed" (or anything unexpected) is a read-only aggregate — ignore as a command
  for (auto *c : climates_) {
    if (sel == 3)
      c->set_off();
    else
      c->set_preset_mode((uint8_t) sel);  // 0=Home / 1=Away / 2=Sleep
  }
  // Optimistic: set_off/set_preset_mode update each climate synchronously, so loop() will agree.
  this->publish_state(value);
  last_published_ = value;
  published_ = true;
  ESP_LOGD(TAG, "all rooms -> %s", value.c_str());
}

void DanfossIconModeSelect::loop() {
  if (climates_.empty())
    return;
  int agg = climates_[0]->active_mode();
  for (auto *c : climates_)
    if (c->active_mode() != agg) {
      agg = -1;  // rooms disagree
      break;
    }
  std::string s = agg < 0 ? MIXED : MODE_NAMES[agg];
  if (published_ && s == last_published_)
    return;  // publish only on change — loop() runs every tick
  last_published_ = s;
  published_ = true;
  this->publish_state(s);
}

void DanfossIconModeSelect::dump_config() {
  LOG_SELECT("", "Danfoss Icon All Rooms Mode", this);
  ESP_LOGCONFIG(TAG, "  Rooms: %u", (unsigned) climates_.size());
}

}  // namespace danfoss_icon
}  // namespace esphome
