#include "danfoss_icon.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace esphome {
namespace danfoss_icon {

static const char *const TAG = "danfoss_icon";

// Tiered per-room poll. FAST = live climate values (temp, active setpoint, heat/cool state) at
// poll_interval_; SLOW = slowly-changing state (identity descriptor/model, fw, battery %, output
// bitmaps), re-read occasionally (self-heals a thermostat asleep at boot). Battery and fault sit on
// the slow tier deliberately — they change on the scale of days / hardware events, not seconds.
// Optional attrs (floor 0x0304, away/asleep, fault 0x03F0) are polled only when their entity is
// enabled, via add_room_poll_attr() — so we never poll what nothing consumes.
static const uint16_t ROOM_FAST[] = {0x0300, 0x0509, 0x1013};
static const size_t ROOM_FAST_N = sizeof(ROOM_FAST) / sizeof(ROOM_FAST[0]);
static const uint16_t ROOM_SLOW[] = {0x0080, 0x007F, 0x030F, 0x1020, 0x1021, 0x1022};
static const size_t ROOM_SLOW_N = sizeof(ROOM_SLOW) / sizeof(ROOM_SLOW[0]);

// Per-controller (rail idx 1-3) identity poll (all slow/static): revision + fw string.
static const uint16_t CONTROLLER_POLL[] = {0x0015, 0x007F};
static const size_t CONTROLLER_POLL_N = sizeof(CONTROLLER_POLL) / sizeof(CONTROLLER_POLL[0]);

// Slow-tier interval for static identity attrs (rooms' identity, controllers, idx0).
static const uint32_t SLOW_POLL_MS = 60000;

// Discovery probe attribute sets, per entity class — what each is decoded into by log_discovery_.
static const uint16_t DISC_CONTROLLER[] = {0x0015, 0x007F, 0x7040, 0x7041};
static const size_t DISC_CONTROLLER_N = sizeof(DISC_CONTROLLER) / sizeof(DISC_CONTROLLER[0]);
static const uint16_t DISC_OUTPUT[] = {0x1008, 0x1200, 0x030C};
static const uint16_t DISC_ROOM[] = {0x0080, 0x007F, 0x1020, 0x1021, 0x1022, 0x0304, 0x030F};
static const size_t DISC_ROOM_N = sizeof(DISC_ROOM) / sizeof(DISC_ROOM[0]);

// Topology: ≤3 controllers, 15 rooms (0x31+) and 15 outputs (0x04+) each. Controller 1 (the gateway)
// answers for the whole linked system. controller m's slots:
static const uint8_t SLOTS_PER_CONTROLLER = 15;
static inline uint8_t room_base(uint8_t m) { return 0x31 + (m - 1) * SLOTS_PER_CONTROLLER; }
static inline uint8_t output_base(uint8_t m) { return 0x04 + (m - 1) * SLOTS_PER_CONTROLLER; }

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
  ESP_LOGCONFIG(TAG, "Danfoss Icon hub starting (%u listener(s))", (unsigned) listeners_.size());
}

void DanfossIconHub::build_poll_list_() {
  poll_list_.clear();
  for (uint8_t idx : controllers_)
    poll_list_.push_back({idx, {CONTROLLER_POLL, CONTROLLER_POLL + CONTROLLER_POLL_N}, "controller", SLOW_POLL_MS});
  // Per room: a fast item (the live climate values) + a slow item (static identity + any attrs
  // registered by optional entities, e.g. floor/away/asleep/fault — so we poll exactly what's enabled).
  for (uint8_t idx : rooms_) {
    poll_list_.push_back({idx, {ROOM_FAST, ROOM_FAST + ROOM_FAST_N}, "room", poll_interval_ms_});
    std::vector<uint16_t> slow(ROOM_SLOW, ROOM_SLOW + ROOM_SLOW_N);
    if (force_manual_) {
      slow.push_back(0x100B);  // room control — read so a scheduled room can be forced to manual
      slow.push_back(0x100A);  // room mode — and an Away/Asleep room forced back to AtHome
    }
    for (const auto &e : extra_room_attrs_)
      if (e.first == idx && std::find(slow.begin(), slow.end(), e.second) == slow.end())  // dedup shared attrs
        slow.push_back(e.second);
    poll_list_.push_back({idx, slow, "room-id", SLOW_POLL_MS});
  }
  // Attrs registered on a non-room idx (e.g. idx0 serial 0x0016) get their own slow item.
  std::vector<uint8_t> handled;
  for (const auto &e : extra_room_attrs_) {
    bool is_room = false;
    for (uint8_t r : rooms_)
      if (r == e.first) {
        is_room = true;
        break;
      }
    bool seen = false;
    for (uint8_t h : handled)
      if (h == e.first) {
        seen = true;
        break;
      }
    if (is_room || seen)
      continue;
    handled.push_back(e.first);
    std::vector<uint16_t> attrs;
    for (const auto &e2 : extra_room_attrs_)
      if (e2.first == e.first)
        attrs.push_back(e2.second);
    poll_list_.push_back({e.first, attrs, "global", SLOW_POLL_MS});
  }
  poll_list_built_ = true;
  ESP_LOGCONFIG(TAG, "Poll list built: %u item(s), %u controller(s), %u room(s)", (unsigned) poll_list_.size(),
                (unsigned) controllers_.size(), (unsigned) rooms_.size());
}

void DanfossIconHub::build_discovery_() {
  // Adaptive one-shot topology probe (logged, not turned into entities): probe controllers
  // 0x01-0x03; each present controller expands to its 15 rooms + 15 outputs
  // (expand_discovery_for_controller_), so absent controllers' slot ranges aren't blind-probed.
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
  uint8_t rb = room_base(controller), ob = output_base(controller);
  for (uint8_t i = 0; i < SLOTS_PER_CONTROLLER; i++)
    discovery_.push_back({(uint8_t) (ob + i), {DISC_OUTPUT, DISC_OUTPUT + 3}, "output"});
  for (uint8_t i = 0; i < SLOTS_PER_CONTROLLER; i++)
    discovery_.push_back({(uint8_t) (rb + i), {DISC_ROOM, DISC_ROOM + DISC_ROOM_N}, "room"});
  ESP_LOGI(TAG, "controller %u present: probing outputs 0x%02X-0x%02X, rooms 0x%02X-0x%02X", controller, ob,
           (unsigned) (ob + SLOTS_PER_CONTROLLER - 1), rb, (unsigned) (rb + SLOTS_PER_CONTROLLER - 1));
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
  }

  update_link_();
  maybe_send_next_();
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
    ESP_LOGW(TAG, "unsolicited 0x0D (no transaction in flight)");
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
      expand_discovery_for_controller_(in_flight_.idx);  // probe this controller's rooms+outputs
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
  uint16_t pid = 0, outlo = 0, outmid = 0, outhi = 0, floor = 0, oavail = 0, ouse = 0;
  uint32_t rev = 0;
  uint8_t batt = 0xFF, usedby = 0xFF, st = 0, duty = 0;
  bool h_floor = false, h_batt = false;
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
      case 0x030F:
        batt = p[0];
        h_batt = true;
        break;
      case 0x0015:
        rev = ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
        break;
      case 0x7040:
        oavail = ((uint16_t) p[0] << 8) | p[1];
        break;
      case 0x7041:
        ouse = ((uint16_t) p[0] << 8) | p[1];
        break;
      case 0x1008:
        usedby = p[0];
        break;
      case 0x1200:
        st = p[0];
        break;
      case 0x030C:
        duty = p[0];
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
    record_discovery_(str_sprintf("Controller %u (idx 0x%02X): fw %s, hw %u.%02u, sw %u.%02u, %u/%u outputs in use",
                                  idx, idx, fw, (unsigned) ((rev >> 24) & 0xFF), (unsigned) ((rev >> 16) & 0xFF),
                                  (unsigned) ((rev >> 8) & 0xFF), (unsigned) (rev & 0xFF),
                                  (unsigned) __builtin_popcount(ouse), (unsigned) __builtin_popcount(oavail)));
    disc_controllers_.push_back(idx);
    return true;
  } else if (idx <= 0x30) {  // actuator output — present only if assigned to a room
    if (usedby == 0x00 || usedby == 0xFF)
      return false;  // unassigned slot (0x00 seen for unused outputs)
    record_discovery_(str_sprintf("Output %u (idx 0x%02X) -> room %u: duty %u%%, auto %s", (unsigned) (idx - 0x03), idx,
                                  usedby, duty, st ? "on" : "off"));
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
    char battbuf[12];
    if (!h_batt)
      strcpy(battbuf, "?");
    else if (batt <= 100)
      snprintf(battbuf, sizeof(battbuf), "%u%%", batt);
    else if (batt == 0xFE)
      strcpy(battbuf, "LOW");
    else if (batt == 0xFF)
      strcpy(battbuf, "wired");
    else
      snprintf(battbuf, sizeof(battbuf), "0x%02X", batt);
    std::string outs;  // actuator channels serving this room (union of the slow/med/fast groups)
    uint16_t out_bits = outlo | outmid | outhi;
    for (int b = 0; b < 16; b++)
      if (out_bits & (1 << b))
        outs += (outs.empty() ? "" : ", ") + str_sprintf("#%d", b + 1);
    record_discovery_(str_sprintf("Room %u (idx 0x%02X): %s, fw %s, battery %s, floor %s, outputs %s",
                                  (unsigned) (idx - 0x30), idx, type, fw, battbuf, floorbuf,
                                  outs.empty() ? "none" : outs.c_str()));
    disc_rooms_.push_back(idx);
    if (h_floor && floor != DI_TEMP_INVALID)
      disc_floor_rooms_.push_back(idx);  // a real floor reading -> floor sensor fitted
    return true;
  }
}

void DanfossIconHub::print_yaml() {
  ESP_LOGI(TAG, "----- discovered config (paste-ready, sub-device grouped) -----");
  // The primary controller (rail idx 1) is implicit = this node device, so it needs no config.
  // Secondary controllers (rail idx 2/3) each get a sub-device. Rooms are always sub-deviced.
  size_t n_secondary = 0;
  for (uint8_t idx : disc_controllers_)
    if (idx > 1)
      n_secondary++;
  const bool any_subdevices = !disc_rooms_.empty() || n_secondary > 0;
  // HA sub-devices go under esphome:.
  if (any_subdevices) {
    ESP_LOGI(TAG, "esphome:");
    ESP_LOGI(TAG, "  devices:");
    for (uint8_t idx : disc_rooms_) {
      unsigned g = idx - 0x31 + 1;
      ESP_LOGI(TAG, "    - id: dev_room_%u", g);
      ESP_LOGI(TAG, "      name: \"Room %u\"", g);
    }
    for (uint8_t idx : disc_controllers_)
      if (idx > 1) {
        ESP_LOGI(TAG, "    - id: dev_secondary_%u", idx);
        ESP_LOGI(TAG, "      name: \"Secondary Controller %u\"", idx - 1);
      }
  }
  // Hub room/secondary-controller lists referencing those devices.
  ESP_LOGI(TAG, "danfoss_icon:");
  if (!disc_rooms_.empty()) {
    ESP_LOGI(TAG, "  rooms:");
    for (uint8_t idx : disc_rooms_) {
      uint8_t rel = idx - 0x31, m = rel / 15 + 1, n = rel % 15 + 1;
      ESP_LOGI(TAG, "    - number: %u", n);
      if (m > 1)
        ESP_LOGI(TAG, "      controller: %u", m);  // rail position; omitted for the primary (1)
      ESP_LOGI(TAG, "      name: \"Room %u\"", (unsigned) (rel + 1));
      ESP_LOGI(TAG, "      device_id: dev_room_%u", (unsigned) (rel + 1));
      // Floor sensor detected (0x0304 reported a real value) -> enable the floor feature set.
      if (std::find(disc_floor_rooms_.begin(), disc_floor_rooms_.end(), idx) != disc_floor_rooms_.end())
        ESP_LOGI(TAG, "      floor: true");
    }
  }
  // Primary controller identity folds onto this node device automatically — no block needed.
  if (n_secondary > 0) {
    ESP_LOGI(TAG, "  secondary_controllers:");
    for (uint8_t idx : disc_controllers_)
      if (idx > 1) {
        ESP_LOGI(TAG, "    - number: %u", idx);  // rail position (2 = first secondary, 3 = second)
        ESP_LOGI(TAG, "      name: \"Secondary Controller %u\"", idx - 1);
        ESP_LOGI(TAG, "      device_id: dev_secondary_%u", idx);
      }
  }
  ESP_LOGI(TAG, "---------------------------------------------------------------");
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
