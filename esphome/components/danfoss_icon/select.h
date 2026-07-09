#pragma once
#include "climate.h"
#include "esphome/core/component.h"
#include "esphome/components/select/select.h"
#include <string>
#include <vector>

namespace esphome {
namespace danfoss_icon {

// Hub-level "All Rooms Mode": a convenience control that sets every room's mode at once
// (Home/Away/Sleep/Off). Setting an option fans the change out as one write per room (there is no
// verified wire broadcast for a per-room attribute). The read state is the aggregate: the shared
// mode, or "Mixed" when the rooms differ — recomputed from the live climate state in loop().
class DanfossIconModeSelect : public select::Select, public Component {
 public:
  void add_climate(DanfossIconClimate *c) { climates_.push_back(c); }
  void loop() override;
  void dump_config() override;

 protected:
  void control(const std::string &value) override;

  std::vector<DanfossIconClimate *> climates_;
  std::string last_published_;
  bool published_{false};
};

}  // namespace danfoss_icon
}  // namespace esphome
