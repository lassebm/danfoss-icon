#pragma once
// Danfoss Icon hub: polls the Icon controller over the RJ45 RS-485 link (we impersonate
// the App Module / poller) and surfaces room state to Home Assistant via ESPHome.
//
// The hub owns the UART, the non-blocking transaction engine, and a write queue.
// Entities (climate / sensor / text_sensor) register as listeners + declare which
// rooms they need polled; the hub slices each 0x0D reply by attribute and dispatches.

#include "protocol.h"
#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include <vector>
#include <string>
#include <utility>

namespace esphome {
namespace danfoss_icon {

// Implemented by entities; the hub calls on_attr() for every parsed (idx, attr, value).
struct DanfossIconListener {
  virtual void on_attr(uint8_t idx, uint16_t attr_id, const uint8_t *data, size_t len) = 0;
};

// One outstanding read: an idx + an ordered attr-id list (reply matched by order).
// interval_ms = how often to poll this item (per-item tiering: fast live values vs slow
// identity + slowly-changing state);
// last_ms = when it was last sent. (Discovery items leave these at 0.)
struct PollItem {
  uint8_t idx;
  std::vector<uint16_t> attrs;
  const char *tag;
  uint32_t interval_ms{0};
  uint32_t last_ms{0};
};

struct WriteReq {
  uint8_t idx;
  uint16_t attr;
  std::vector<uint8_t> value;
};

// One poll registration from an entity: poll (attr) on (idx) at the fast or slow tier.
// build_poll_list_ groups these by idx and de-dups; an attr registered on both tiers polls fast.
struct PollReg {
  uint8_t idx;
  uint16_t attr;
  bool fast;
};

// The transaction currently on the wire (awaiting a 0x0D reply).
struct InFlight {
  bool is_write{false};
  bool discovery{false};  // a one-shot topology-probe read (logged, not dispatched)
  uint8_t idx{0};
  std::vector<uint16_t> attrs;  // read: full list; write: single attr
  std::vector<uint8_t> value;   // write: the bytes sent (dispatched to listeners on OK)
  const char *tag{"?"};
};

enum class TxState { IDLE, WAITING };

class DanfossIconHub : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Config + entity registration.
  void add_room(uint8_t idx);        // dedup; tracks polled rooms for the dump_config summary
  void add_controller(uint8_t idx);  // dedup; tracks polled controllers for the dump_config summary
  // Register an attribute for polling on (idx), at the fast (poll_interval) or slow (60 s) tier.
  // Entities call these for exactly what they consume; build_poll_list_ groups by idx and de-dups
  // (an attr registered on both tiers polls fast). Nothing is polled that no entity registered.
  void add_fast_attr(uint8_t idx, uint16_t attr) { poll_regs_.push_back({idx, attr, true}); }
  void add_slow_attr(uint8_t idx, uint16_t attr) { poll_regs_.push_back({idx, attr, false}); }
  void set_poll_interval(uint32_t ms) { poll_interval_ms_ = ms; }
  void set_reply_timeout(uint32_t ms) { reply_timeout_ms_ = ms; }
  void add_listener(DanfossIconListener *l) { listeners_.push_back(l); }

  // Queue a write (e.g. a HA setpoint change). Prioritized ahead of polling reads.
  void queue_write(uint8_t idx, uint16_t attr_id, const uint8_t *value, uint8_t len);

  // Log a paste-ready YAML config built from the discovered rooms/controllers (button action).
  void print_yaml();

  // Optional controller-link indicator (device_class: connectivity). On = controller responding. This is
  // the ESP<->controller RJ45 link (our side); per-room radio links are separate (room Fault text).
  void set_link_sensor(binary_sensor::BinarySensor *s) { link_sensor_ = s; }
  void set_link_timeout(uint32_t ms) { link_timeout_ms_ = ms; }
  // On boot, force any room found running a schedule (room control 0x100B != 0) back to manual
  // (0x100B=0) + AtHome (0x100A=0) so HA owns the active setpoint (0x0509). See dispatch_read_reply_.
  void set_force_manual(bool b) { force_manual_ = b; }

 protected:
  void build_poll_list_();
  void build_discovery_();
  void expand_discovery_for_controller_(uint8_t controller);
  void pump_rx_();
  void handle_frame_(const uint8_t *f, size_t len);
  void dispatch_read_reply_(const uint8_t *val, size_t vlen);
  // Decode+log one discovery reply. Returns true if the slot is present/populated.
  bool log_discovery_(uint8_t idx, const std::vector<uint16_t> &attrs, const uint8_t *val, size_t vlen);
  void record_discovery_(const std::string &line);
  void maybe_send_next_();
  void send_read_(const PollItem &item, bool discovery);
  void send_write_(const WriteReq &w);
  void finish_transaction_();
  void update_link_();

  std::vector<uint8_t> rooms_;
  std::vector<uint8_t> controllers_;
  std::vector<PollReg> poll_regs_;  // (idx, attr, tier) registrations from entities -> poll_list_
  std::vector<PollItem> poll_list_;
  std::vector<PollItem> discovery_;
  std::vector<std::string> disc_inventory_;  // decoded lines, replayed by dump_config()
  std::vector<std::string> yaml_pending_;    // print_yaml() output, drained a few lines/loop
  size_t yaml_idx_{0};                       // next yaml_pending_ line to log
  std::vector<uint8_t> disc_rooms_;          // present room idxs (for print_yaml)
  std::vector<uint8_t> disc_floor_rooms_;    // subset of disc_rooms_ with a floor sensor fitted
  std::vector<uint8_t> disc_controllers_;    // present controller idxs (for print_yaml)
  std::vector<WriteReq> write_queue_;
  std::vector<DanfossIconListener *> listeners_;
  bool poll_list_built_{false};
  bool disc_active_{true};
  size_t disc_idx_{0};
  size_t poll_idx_{0};

  std::vector<uint8_t> rx_buf_;
  TxState state_{TxState::IDLE};
  InFlight in_flight_;
  uint32_t tx_time_ms_{0};
  uint32_t poll_interval_ms_{2000};  // fast tier (dynamic attrs)
  uint32_t reply_timeout_ms_{250};   // wait for a 0x0D reply before timing out the transaction
  bool force_manual_{true};          // boot: reset scheduled rooms to manual/AtHome (HA-authoritative)

  // Controller-link tracking: any received 0x0D means the controller is responding. No "link up" bit
  // exists, so the link is inferred from reply silence — hence a (legitimate) timeout here.
  binary_sensor::BinarySensor *link_sensor_{nullptr};
  uint32_t last_reply_ms_{0};
  uint32_t link_timeout_ms_{15000};
  bool got_reply_{false};
  bool last_connected_{false};
  bool link_published_{false};
};

}  // namespace danfoss_icon
}  // namespace esphome
