#include "danfoss_icon.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace esphome {
namespace danfoss_icon {

static const char *const TAG = "danfoss_icon";

// Two poll tiers: FAST = live values at poll_interval_ (e.g. the climate's temp/setpoint/state, and
// floor temp for floor-regulated rooms); SLOW = slowly-changing identity/state at 60 s (model, fw,
// battery, output bitmaps, setpoint bounds, faults, …). Each entity registers the attrs it consumes
// (add_fast_attr/add_slow_attr); build_poll_list_ groups them by idx and de-dups (fast wins over
// slow), so we poll exactly what's enabled.
static const uint32_t SLOW_POLL_MS = 60000;

// Discovery probe attribute sets, per entity class — decoded into a logged inventory by
// log_discovery_(). Per controller: presence/version + fw. Per room: presence/type, fw,
// floor-sensor presence, and the output bitmaps (which actuator channels serve the room).
static const uint16_t DISC_CONTROLLER[] = {0x0015, 0x007F};
static const size_t DISC_CONTROLLER_N = sizeof(DISC_CONTROLLER) / sizeof(DISC_CONTROLLER[0]);
static const uint16_t DISC_ROOM[] = {0x0080, 0x007F, 0x1020, 0x1021, 0x1022, 0x0304};
static const size_t DISC_ROOM_N = sizeof(DISC_ROOM) / sizeof(DISC_ROOM[0]);

// Topology: ≤3 controllers, 15 rooms (0x31+) and 15 outputs (0x04+) each. Controller 1 (the gateway)
// answers for the whole linked system. Rooms are addressed directly; controller m's room base:
static const uint8_t SLOTS_PER_CONTROLLER = 15;
static inline uint8_t room_base(uint8_t m) { return 0x31 + (m - 1) * SLOTS_PER_CONTROLLER; }

void DanfossIconHub::add_room(uint8_t idx) {
  for (uint8_t r : rooms_)
    if (r == idx)
      return;
  rooms_.push_back(idx);
}

void DanfossIconHub::add_controller(uint8_t idx) {
  for (uint8_t m : controllers_)
    if (m == idx)
      return;
  controllers_.push_back(idx);
}

void DanfossIconHub::setup() {
  rx_buf_.reserve(128);
  // Heartbeat: always fast-poll the primary controller's revision (0x0015) — a static, always-present
  // read that keeps the Connection sensor's link measurement alive whatever entities are configured.
  // De-duped against a controller identity entity that also reads it (fast wins).
  add_fast_attr(0x01, 0x0015);
  ESP_LOGCONFIG(TAG, "Danfoss Icon hub starting (%u listener(s))", (unsigned) listeners_.size());
}

void DanfossIconHub::build_poll_list_() {
  poll_list_.clear();
  // Collect the distinct idxs that have any registration, in first-seen order.
  std::vector<uint8_t> idxs;
  for (const auto &r : poll_regs_)
    if (std::find(idxs.begin(), idxs.end(), r.idx) == idxs.end())
      idxs.push_back(r.idx);
  // Per idx: one fast item (its fast attrs) at poll_interval_, one slow item (its slow attrs, minus
  // any already fast) at 60 s. Tag is derived from the idx range (idx0 = global, 1-3 = controller).
  for (uint8_t idx : idxs) {
    std::vector<uint16_t> fast, slow;
    for (const auto &r : poll_regs_)
      if (r.idx == idx && r.fast && std::find(fast.begin(), fast.end(), r.attr) == fast.end())
        fast.push_back(r.attr);
    for (const auto &r : poll_regs_)
      if (r.idx == idx && !r.fast && std::find(fast.begin(), fast.end(), r.attr) == fast.end() &&
          std::find(slow.begin(), slow.end(), r.attr) == slow.end())
        slow.push_back(r.attr);
    // force_manual: on boot, read each room's control/mode so a scheduled or Away/Asleep room can be
    // reset to manual + AtHome (HA owns the active setpoint). Slow tier, de-duped like the rest.
    if (force_manual_ && idx >= 0x31)
      for (uint16_t a : {(uint16_t) 0x100B, (uint16_t) 0x100A})
        if (std::find(fast.begin(), fast.end(), a) == fast.end() &&
            std::find(slow.begin(), slow.end(), a) == slow.end())
          slow.push_back(a);
    const char *tag = idx == 0x00 ? "global" : (idx <= 0x03 ? "controller" : "room");
    if (!fast.empty())
      poll_list_.push_back({idx, fast, tag, poll_interval_ms_});
    if (!slow.empty())
      poll_list_.push_back({idx, slow, tag, SLOW_POLL_MS});
  }
  poll_list_built_ = true;
  ESP_LOGCONFIG(TAG, "Poll list built: %u item(s), %u controller(s), %u room(s)", (unsigned) poll_list_.size(),
                (unsigned) controllers_.size(), (unsigned) rooms_.size());
}

void DanfossIconHub::build_discovery_() {
  // Adaptive one-shot topology probe (logged, not turned into entities): probe controllers
  // 0x01-0x03; each present controller expands to its 15 rooms
  // (expand_discovery_for_controller_), so absent controllers' room ranges aren't blind-probed.
  discovery_.clear();
  disc_inventory_.clear();
  disc_rooms_.clear();
  disc_floor_rooms_.clear();
  disc_controllers_.clear();
  for (uint8_t idx = 0x01; idx <= 0x03; idx++)
    discovery_.push_back({idx, {DISC_CONTROLLER, DISC_CONTROLLER + DISC_CONTROLLER_N}, "controller"});
  ESP_LOGI(TAG, "discovery: probing controllers 0x01-0x03");
}

void DanfossIconHub::expand_discovery_for_controller_(uint8_t controller) {
  uint8_t rb = room_base(controller);
  for (uint8_t i = 0; i < SLOTS_PER_CONTROLLER; i++)
    discovery_.push_back({(uint8_t) (rb + i), {DISC_ROOM, DISC_ROOM + DISC_ROOM_N}, "room"});
  ESP_LOGI(TAG, "controller %u present: probing rooms 0x%02X-0x%02X", controller, rb,
           (unsigned) (rb + SLOTS_PER_CONTROLLER - 1));
}

void DanfossIconHub::loop() {
  // Built lazily on first loop so entities registering rooms in their setup() are all in.
  if (!poll_list_built_) {
    build_poll_list_();
    build_discovery_();
  }

  pump_rx_();

  if (state_ == TxState::WAITING && (millis() - tx_time_ms_) > reply_timeout_ms_) {
    ESP_LOGW(TAG, "timeout on %s idx=0x%02X", in_flight_.tag, in_flight_.idx);
    finish_transaction_();
    // The reply may still be in transit. Hold the bus until it has been quiet for one timeout window
    // so the stale reply is discarded as unsolicited rather than misattributed to the next request.
    resync_quiet_until_ms_ = millis() + reply_timeout_ms_;
  }

  update_link_();
  maybe_send_next_();

  // Drain any pending paste-ready YAML a few lines per loop (see print_yaml): logging it all at once
  // would block one loop long enough to trip the slow-loop watchdog, especially over the API link.
  for (int i = 0; i < 5 && yaml_idx_ < yaml_pending_.size(); i++)
    ESP_LOGI(TAG, "%s", yaml_pending_[yaml_idx_++].c_str());
  if (yaml_idx_ > 0 && yaml_idx_ >= yaml_pending_.size()) {
    yaml_pending_.clear();
    yaml_idx_ = 0;
  }
}

void DanfossIconHub::update_link_() {
  if (link_sensor_ == nullptr)
    return;
  // Disconnected until the first reply, then if nothing's been heard for link_timeout_ms_.
  bool connected = got_reply_ && (millis() - last_reply_ms_) <= link_timeout_ms_;
  if (!link_published_ || connected != last_connected_) {
    link_sensor_->publish_state(connected);  // device_class connectivity: on = connected
    last_connected_ = connected;
    link_published_ = true;
  }
}

void DanfossIconHub::pump_rx_() {
  while (this->available()) {
    uint8_t b;
    if (!this->read_byte(&b))
      break;
    rx_buf_.push_back(b);
    if (rx_buf_.size() > 512)
      rx_buf_.erase(rx_buf_.begin(), rx_buf_.begin() + 256);
  }

  while (!rx_buf_.empty()) {
    FrameScan r = scan_frame(rx_buf_.data(), rx_buf_.size());
    if (r.found)
      handle_frame_(rx_buf_.data() + r.start, r.len);
    else if (r.bad_crc)
      ESP_LOGW(TAG, "bad CRC, len=%u — resyncing", (unsigned) r.len);
    if (r.consumed == 0)
      break;
    rx_buf_.erase(rx_buf_.begin(), rx_buf_.begin() + r.consumed);
    if (!r.found && !r.bad_crc)
      break;
  }
}

void DanfossIconHub::handle_frame_(const uint8_t *f, size_t len) {
  uint8_t cmd = f[3];
  if (cmd != DI_CMD_RESPONSE) {
    ESP_LOGD(TAG, "ignoring non-0x0D frame cmd=0x%02X", cmd);
    return;
  }
  if (state_ != TxState::WAITING) {
    if (millis() < resync_quiet_until_ms_) {
      // Expected: the late reply for a just-timed-out request. Discard it and keep draining until the
      // bus stays quiet for a full window, so we don't misattribute it to the next request.
      ESP_LOGD(TAG, "discarded stale 0x0D after timeout (resync)");
      resync_quiet_until_ms_ = millis() + reply_timeout_ms_;
    } else {
      ESP_LOGW(TAG, "unsolicited 0x0D (no transaction in flight)");
    }
    return;
  }
  // A matched 0x0D means the controller is responding (even a non-OK status = link alive).
  got_reply_ = true;
  last_reply_ms_ = millis();
  uint8_t status = f[4];
  const uint8_t *val = f + 5;
  size_t vlen = (len >= 7) ? (len - 7) : 0;

  if (in_flight_.is_write) {
    if (status != DI_STATUS_OK) {
      ESP_LOGW(TAG, "write idx=0x%02X attr=0x%04X rejected status=0x%02X (%s)", in_flight_.idx,
               in_flight_.attrs.empty() ? 0 : in_flight_.attrs[0], status, status_name(status));
    } else {
      ESP_LOGD(TAG, "write idx=0x%02X attr=0x%04X OK", in_flight_.idx,
               in_flight_.attrs.empty() ? 0 : in_flight_.attrs[0]);
      // Write accepted: push the value we sent to listeners now (e.g. the climate's setpoint bounds
      // follow an edit immediately) instead of waiting for the next poll. The slow poll still
      // backstops any controller-side adjustment. Notifies on_attr only — no bus write, so no loop.
      if (!in_flight_.attrs.empty())
        for (auto *l : listeners_)
          l->on_attr(in_flight_.idx, in_flight_.attrs[0], in_flight_.value.data(), in_flight_.value.size());
    }
    finish_transaction_();
    return;
  }

  bool disc = in_flight_.discovery;
  if (status != DI_STATUS_OK) {
    if (!disc)  // a discovery probe of an absent/unsupported slot — skip quietly
      ESP_LOGW(TAG, "read %s idx=0x%02X status=0x%02X (%s)", in_flight_.tag, in_flight_.idx, status,
               status_name(status));
  } else if (disc) {
    bool present = log_discovery_(in_flight_.idx, in_flight_.attrs, val, vlen);
    if (present && in_flight_.idx >= 0x01 && in_flight_.idx <= 0x03)
      expand_discovery_for_controller_(in_flight_.idx);  // probe this controller's rooms
  } else {
    dispatch_read_reply_(val, vlen);
  }
  finish_transaction_();
}

void DanfossIconHub::dispatch_read_reply_(const uint8_t *val, size_t vlen) {
  const uint8_t *p = val;
  size_t remaining = vlen;
  for (uint16_t attr : in_flight_.attrs) {
    size_t sz = attr_value_size(attr);
    if (sz == 0) {
      ESP_LOGW(TAG, "unknown size for attr 0x%04X — stopping slice", attr);
      return;
    }
    if (sz > remaining) {
      ESP_LOGW(TAG, "short reply: attr 0x%04X needs %u, %u left", attr, (unsigned) sz, (unsigned) remaining);
      return;
    }
    for (auto *l : listeners_)
      l->on_attr(in_flight_.idx, attr, p, sz);
    // force_manual: HA owns the active setpoint (0x0509), which the firmware uses only when the
    // room is BOTH manual (room control 0x100B == 0) AND AtHome (room mode 0x100A == 0) — otherwise
    // it regulates to a schedule (0x100B) or to the Away/Asleep preset (0x100A). Reset whichever
    // is non-zero, independently. Self-healing — re-issued each slow poll until the read is 0.
    if (force_manual_ && in_flight_.idx >= 0x31 && sz >= 1 && p[0] != 0) {
      const uint8_t zero = 0x00;
      if (attr == 0x100B) {
        queue_write(in_flight_.idx, 0x100B, &zero, 1);  // room control -> manual
        ESP_LOGI(TAG, "force_manual: idx=0x%02X scheduled (0x100B=%u) -> manual", in_flight_.idx, p[0]);
      } else if (attr == 0x100A) {
        queue_write(in_flight_.idx, 0x100A, &zero, 1);  // room mode -> AtHome
        ESP_LOGI(TAG, "force_manual: idx=0x%02X mode (0x100A=%u) -> AtHome", in_flight_.idx, p[0]);
      }
    }
    p += sz;
    remaining -= sz;
  }
}

void DanfossIconHub::finish_transaction_() {
  // poll_idx_ is advanced by the scheduler when it picks the next due item.
  state_ = TxState::IDLE;
  in_flight_ = InFlight{};
}

void DanfossIconHub::queue_write(uint8_t idx, uint16_t attr_id, const uint8_t *value, uint8_t len) {
  write_queue_.push_back({idx, attr_id, std::vector<uint8_t>(value, value + len)});
  ESP_LOGD(TAG, "queued write idx=0x%02X attr=0x%04X (%u queued)", idx, attr_id, (unsigned) write_queue_.size());
}

void DanfossIconHub::maybe_send_next_() {
  if (state_ != TxState::IDLE)
    return;
  // After a timeout, hold off until the bus has been quiet for a full window (see resync_quiet_until_ms_)
  // so a late, stale reply is dropped as unsolicited instead of being misattributed to this request.
  if (resync_quiet_until_ms_ != 0) {
    if (millis() < resync_quiet_until_ms_)
      return;
    resync_quiet_until_ms_ = 0;
  }
  if (!write_queue_.empty()) {  // writes jump the queue ahead of polling
    send_write_(write_queue_.front());
    write_queue_.erase(write_queue_.begin());
    return;
  }
  if (disc_active_) {  // run the one-shot discovery probe before steady polling
    if (disc_idx_ < discovery_.size()) {
      send_read_(discovery_[disc_idx_], /*discovery=*/true);
      disc_idx_++;
      return;
    }
    disc_active_ = false;
    ESP_LOGI(TAG, "discovery complete");
  }
  if (poll_list_.empty())
    return;
  // Per-item tiering: round-robin from poll_idx_ for the next item whose interval has elapsed.
  uint32_t now = millis();
  size_t n = poll_list_.size();
  for (size_t k = 0; k < n; k++) {
    size_t i = (poll_idx_ + k) % n;
    PollItem &it = poll_list_[i];
    // last_ms == 0 = never polled → due immediately (poll everything once at boot, then
    // on each item's interval). millis() is past 0 by the time we steady-poll.
    if (it.last_ms == 0 || (now - it.last_ms) >= it.interval_ms) {
      it.last_ms = now;
      poll_idx_ = (i + 1) % n;
      send_read_(it, /*discovery=*/false);
      return;
    }
  }
}

void DanfossIconHub::send_read_(const PollItem &item, bool discovery) {
  uint8_t buf[80];
  size_t n = build_read(buf, sizeof(buf), item.idx, item.attrs.data(), (uint8_t) item.attrs.size());
  if (n == 0) {
    ESP_LOGE(TAG, "build_read failed for %s idx=0x%02X", item.tag, item.idx);
    return;
  }
  this->write_array(buf, n);
  this->flush();
  in_flight_ = InFlight{false, discovery, item.idx, item.attrs, {}, item.tag};
  state_ = TxState::WAITING;
  tx_time_ms_ = millis();
}

void DanfossIconHub::send_write_(const WriteReq &w) {
  uint8_t buf[32];
  size_t n = build_write(buf, sizeof(buf), w.idx, w.attr, w.value.data(), (uint8_t) w.value.size());
  if (n == 0) {
    ESP_LOGE(TAG, "build_write failed idx=0x%02X attr=0x%04X", w.idx, w.attr);
    return;
  }
  this->write_array(buf, n);
  this->flush();
  in_flight_ = InFlight{true, false, w.idx, {w.attr}, w.value, "write"};
  state_ = TxState::WAITING;
  tx_time_ms_ = millis();
  ESP_LOGD(TAG, "-> write idx=0x%02X attr=0x%04X: %s", w.idx, w.attr, format_hex_pretty(buf, n).c_str());
}

// Slice a discovery reply and log a decoded, human-readable inventory line.
// Returns true if the slot is present/populated.
bool DanfossIconHub::log_discovery_(uint8_t idx, const std::vector<uint16_t> &attrs, const uint8_t *val, size_t vlen) {
  const uint8_t *p = val;
  size_t rem = vlen;
  uint16_t pid = 0, outlo = 0, outmid = 0, outhi = 0, floor = 0;
  uint32_t rev = 0;
  bool h_floor = false;
  char fw[20] = {0};
  uint8_t desc[7];
  size_t desclen = 0;

  for (uint16_t a : attrs) {
    size_t sz = attr_value_size(a);
    if (sz == 0 || sz > rem)
      break;
    switch (a) {
      case 0x0080:  // descriptor: [00][node][rail][00][product u16 BE @4:5][00=present/FF=empty]
        desclen = sz < sizeof(desc) ? sz : sizeof(desc);
        memcpy(desc, p, desclen);
        if (desclen >= 6)
          pid = ((uint16_t) desc[4] << 8) | desc[5];
        break;
      case 0x007F: {  // length-prefixed ASCII fw
        size_t n = p[0];
        if (n > sz - 1)
          n = sz - 1;
        if (n > sizeof(fw) - 1)
          n = sizeof(fw) - 1;
        size_t j = 0;
        for (; j < n && p[1 + j] != 0; j++)
          fw[j] = (char) p[1 + j];
        fw[j] = 0;
        break;
      }
      case 0x1020:
        outlo = ((uint16_t) p[0] << 8) | p[1];
        break;
      case 0x1021:
        outmid = ((uint16_t) p[0] << 8) | p[1];
        break;
      case 0x1022:
        outhi = ((uint16_t) p[0] << 8) | p[1];
        break;
      case 0x0304:
        floor = ((uint16_t) p[0] << 8) | p[1];
        h_floor = true;
        break;
      case 0x0015:
        rev = ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
        break;
      default:
        break;
    }
    p += sz;
    rem -= sz;
  }

  if (idx <= 0x03) {  // controller / rail — present only if it has a real revision
    if (rev == 0)
      return false;  // absent controller (idx 2/3 reply with zeroed revision)
    record_discovery_(str_sprintf("Controller %u (idx 0x%02X): fw %s, hw %u.%02u, sw %u.%02u", idx, idx, fw,
                                  (unsigned) ((rev >> 24) & 0xFF), (unsigned) ((rev >> 16) & 0xFF),
                                  (unsigned) ((rev >> 8) & 0xFF), (unsigned) (rev & 0xFF)));
    disc_controllers_.push_back(idx);
    return true;
  } else {  // room / thermostat — present only if it has a product id (descriptor [4:5])
    if (pid == 0)
      return false;  // empty room slot (empty descriptor's [4:5] = 0, byte[6] = 0xFF)
    const char *type = product_id_name(pid);
    char typebuf[28];
    if (type == nullptr) {
      snprintf(typebuf, sizeof(typebuf), "unknown(0x%04X)", pid);
      type = typebuf;
    }
    char floorbuf[16];
    if (h_floor && floor != DI_TEMP_INVALID)
      snprintf(floorbuf, sizeof(floorbuf), "%.2fC", floor / 100.0f);
    else
      strcpy(floorbuf, "none");
    std::string outs;  // actuator channels serving this room (union of the slow/med/fast groups)
    uint16_t out_bits = outlo | outmid | outhi;
    for (int b = 0; b < 16; b++)
      if (out_bits & (1 << b))
        outs += (outs.empty() ? "" : ", ") + str_sprintf("#%d", b + 1);
    record_discovery_(str_sprintf("Room %u (idx 0x%02X): %s, fw %s, floor %s, outputs %s", (unsigned) (idx - 0x30), idx,
                                  type, fw, floorbuf, outs.empty() ? "none" : outs.c_str()));
    disc_rooms_.push_back(idx);
    if (h_floor && floor != DI_TEMP_INVALID)
      disc_floor_rooms_.push_back(idx);  // a real floor reading -> floor sensor fitted
    return true;
  }
}

void DanfossIconHub::print_yaml() {
  // Build the paste-ready config into a queue (fast — string formatting only); loop() drains it a
  // few lines per iteration, so a large dump never blocks one loop past the slow-loop watchdog.
  yaml_pending_.clear();
  yaml_idx_ = 0;
  auto emit = [this](std::string s) { yaml_pending_.push_back(std::move(s)); };

  emit("----- discovered config (paste-ready, sub-device grouped) -----");
  // The primary controller (rail idx 1) is implicit = this node device, so it needs no config.
  // Secondary controllers (rail idx 2/3) each get a sub-device. Rooms are always sub-deviced.
  size_t n_secondary = 0;
  for (uint8_t idx : disc_controllers_)
    if (idx > 1)
      n_secondary++;
  // HA sub-devices go under esphome:.
  if (!disc_rooms_.empty() || n_secondary > 0) {
    emit("esphome:");
    emit("  devices:");
    for (uint8_t idx : disc_rooms_) {
      unsigned g = idx - 0x31 + 1;
      emit(str_sprintf("    - id: dev_room_%u", g));
      emit(str_sprintf("      name: \"Room %u\"", g));
    }
    for (uint8_t idx : disc_controllers_)
      if (idx > 1) {
        emit(str_sprintf("    - id: dev_secondary_%u", idx));
        emit(str_sprintf("      name: \"Secondary Controller %u\"", idx - 1));
      }
  }
  // Hub room/secondary-controller lists referencing those devices.
  emit("danfoss_icon:");
  if (!disc_rooms_.empty()) {
    emit("  rooms:");
    for (uint8_t idx : disc_rooms_) {
      uint8_t rel = idx - 0x31, m = rel / 15 + 1, n = rel % 15 + 1;
      emit(str_sprintf("    - number: %u", n));
      if (m > 1)
        emit(str_sprintf("      controller: %u", m));  // rail position; omitted for the primary (1)
      emit(str_sprintf("      name: \"Room %u\"", (unsigned) (rel + 1)));
      emit(str_sprintf("      device_id: dev_room_%u", (unsigned) (rel + 1)));
      // Floor sensor detected (0x0304 reported a real value) -> enable the floor feature set.
      if (std::find(disc_floor_rooms_.begin(), disc_floor_rooms_.end(), idx) != disc_floor_rooms_.end())
        emit("      floor: true");
    }
  }
  // Primary controller identity folds onto this node device automatically — no block needed.
  if (n_secondary > 0) {
    emit("  secondary_controllers:");
    for (uint8_t idx : disc_controllers_)
      if (idx > 1) {
        emit(str_sprintf("    - number: %u", idx));  // rail position (2 = first secondary, 3 = second)
        emit(str_sprintf("      name: \"Secondary Controller %u\"", idx - 1));
        emit(str_sprintf("      device_id: dev_secondary_%u", idx));
      }
  }
  emit("---------------------------------------------------------------");
}

// Log a discovery line live and cache it so dump_config() can replay it on every connect.
void DanfossIconHub::record_discovery_(const std::string &line) {
  ESP_LOGI(TAG, "%s", line.c_str());
  disc_inventory_.push_back(line);
}

void DanfossIconHub::dump_config() {
  ESP_LOGCONFIG(TAG, "Danfoss Icon:");
  ESP_LOGCONFIG(TAG, "  Poll interval: %u ms", (unsigned) poll_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Reply timeout: %u ms", (unsigned) reply_timeout_ms_);
  ESP_LOGCONFIG(TAG, "  Rooms polled: %u", (unsigned) rooms_.size());
  this->check_uart_settings(115200);
  // Replayed on every client connect (esphome logs / HA subscribe with dump_config=true).
  if (disc_inventory_.empty()) {
    ESP_LOGCONFIG(TAG, "  Discovery: (pending — probe runs at boot)");
  } else {
    ESP_LOGCONFIG(TAG, "  Discovered inventory (%u):", (unsigned) disc_inventory_.size());
    for (const auto &line : disc_inventory_)
      ESP_LOGCONFIG(TAG, "    %s", line.c_str());
  }
}

}  // namespace danfoss_icon
}  // namespace esphome
