#pragma once
#include "danfoss_icon.h"
#include "esphome/components/button/button.h"

namespace esphome {
namespace danfoss_icon {

// Diagnostic button: on press, logs a paste-ready YAML config of the discovered topology.
class DanfossIconYamlButton : public button::Button {
 public:
  void set_parent(DanfossIconHub *parent) { parent_ = parent; }

 protected:
  void press_action() override { parent_->print_yaml(); }
  DanfossIconHub *parent_{nullptr};
};

}  // namespace danfoss_icon
}  // namespace esphome
