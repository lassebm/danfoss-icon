#pragma once
// Danfoss Icon controller <-> App Module "Cumulus" wire protocol.
//
// Pure C++ (no ESPHome dependency) so it can be unit-tested on the host.
// The frame layout below is the complete reference for the link.
//
// Frame:  [0x01 sync][len][0x01 fixed][cmd][idx/status][...payload...][crc_lo][crc_hi]
//   len  = TOTAL frame length (sync .. crc_hi inclusive)
//   crc  = CRC-16/MODBUS over bytes[0 .. len-3], little-endian on the wire
//   attr ids are uint16 BIG-endian; values are BIG-endian.

#include <cstdint>
#include <cstddef>

namespace esphome {
namespace danfoss_icon {

static const uint8_t DI_SYNC = 0x01;
static const uint8_t DI_FIXED = 0x01;
static const uint8_t DI_CMD_COMMAND = 0x0C;   // App Module -> controller (read/write)
static const uint8_t DI_CMD_RESPONSE = 0x0D;  // controller -> App Module
// 0x0D response status byte: 0x00 = OK; non-zero = the controller rejected the command (see
// status_name()). These are the codes its attribute store-writer returns on the read/write path.
static const uint8_t DI_STATUS_OK = 0x00;
static const uint8_t DI_STATUS_NOT_FOUND_READ = 0x02;   // no descriptor for this attribute (read)
static const uint8_t DI_STATUS_NOT_FOUND_WRITE = 0x03;  // no descriptor for this attribute (write)
static const uint8_t DI_STATUS_OUT_OF_RANGE = 0x04;     // value outside the attribute's min/max
static const uint8_t DI_STATUS_READ_ONLY = 0x05;        // attribute is read-only / protected
static const uint8_t DI_STATUS_LENGTH = 0x07;           // value shorter than the attribute size

static const uint8_t DI_FRAME_MIN = 7;    // structural minimum (empty-value 0x0D)
static const uint8_t DI_FRAME_MAX = 128;  // generous cap; observed up to ~61

// 0x8000 = invalid / no-sensor / unconfigured sentinel for temp-class u16 values.
static const uint16_t DI_TEMP_INVALID = 0x8000;

// CRC-16/MODBUS: init 0xFFFF, reflected poly 0xA001.
inline uint16_t crc16_modbus(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
      crc = (crc & 1) ? (uint16_t) ((crc >> 1) ^ 0xA001) : (uint16_t) (crc >> 1);
  }
  return crc;
}

// Append the CRC over buf[0..n-1] at buf[n], buf[n+1] (LE). Returns total length n+2.
inline size_t append_crc(uint8_t *buf, size_t n) {
  uint16_t crc = crc16_modbus(buf, n);
  buf[n] = (uint8_t) (crc & 0xFF);
  buf[n + 1] = (uint8_t) (crc >> 8);
  return n + 2;
}

// Build a READ command: 01 [len] 01 0C [idx] [n2<<4] [attr_id u16BE ...] crc
// `count` read attributes (secondary/n2). buf must hold 8 + 2*count bytes
// (6 header + 2*count attr ids + 2 crc).
// Returns frame length, or 0 on overflow / invalid count (count must be 1..15).
inline size_t build_read(uint8_t *buf, size_t cap, uint8_t idx, const uint16_t *attr_ids, uint8_t count) {
  if (count < 1 || count > 15)
    return 0;
  size_t total = 8 + 2u * count;
  if (cap < total)
    return 0;
  size_t i = 0;
  buf[i++] = DI_SYNC;
  buf[i++] = (uint8_t) total;
  buf[i++] = DI_FIXED;
  buf[i++] = DI_CMD_COMMAND;
  buf[i++] = idx;
  buf[i++] = (uint8_t) (count << 4);  // n2 in the high nibble, n1=0
  for (uint8_t a = 0; a < count; a++) {
    buf[i++] = (uint8_t) (attr_ids[a] >> 8);
    buf[i++] = (uint8_t) (attr_ids[a] & 0xFF);
  }
  return append_crc(buf, i);
}

// Build a single-attribute WRITE: 01 [len] 01 0C [idx] [01] [attr_id u16BE] [value...] crc
// (n1 = 1 in the low nibble.) buf must hold 10 + vlen bytes
// (6 header + 2 attr id + vlen value + 2 crc).
inline size_t build_write(uint8_t *buf, size_t cap, uint8_t idx, uint16_t attr_id, const uint8_t *value, uint8_t vlen) {
  size_t total = 10 + (size_t) vlen;
  if (cap < total)
    return 0;
  size_t i = 0;
  buf[i++] = DI_SYNC;
  buf[i++] = (uint8_t) total;
  buf[i++] = DI_FIXED;
  buf[i++] = DI_CMD_COMMAND;
  buf[i++] = idx;
  buf[i++] = 0x01;  // n1 = 1 (primary/write), n2 = 0
  buf[i++] = (uint8_t) (attr_id >> 8);
  buf[i++] = (uint8_t) (attr_id & 0xFF);
  for (uint8_t v = 0; v < vlen; v++)
    buf[i++] = value[v];
  return append_crc(buf, i);
}

// Value size (bytes) for the attributes this component reads/writes.
// The controller's 0x0D reply has no attr echo, so values are sliced by the request's attr
// order using these sizes. Returns 0 for ids not in our set (caller must not slice).
inline size_t attr_value_size(uint16_t attr_id) {
  switch (attr_id) {
    // 2-byte: temps / setpoints / u16 identity & rail params
    case 0x0300:
    case 0x0304:
    case 0x0308:
    case 0x0311:
    case 0x0312:
    case 0x0313:
    case 0x0320:
    case 0x0321:
    case 0x0322:
    case 0x0329:
    case 0x0330:
    case 0x03F0:
    case 0x0507:
    case 0x0508:
    case 0x0509:
    case 0x050A:
    case 0x050B:
    case 0x050C:
    case 0x050D:
    case 0x1020:
    case 0x1021:
    case 0x1022:
    case 0x0017:
    case 0x0023:
    case 0x0040:
    case 0x1078:
    case 0x107A:
    case 0x107B:
    case 0x107D:
      return 2;
    // 1-byte: enums / flags / percent
    case 0x030A:
    case 0x030F:
    case 0x100A:
    case 0x100B:
    case 0x1013:
    case 0x1201:
    case 0x1100:
    case 0x1101:
    case 0x0005:
    case 0x007E:
    case 0x1001:
      return 1;
    // 4-byte: timestamps / serial / revision
    case 0x0004:
    case 0x0015:
    case 0x0016:
      return 4;
    case 0x007F:
      return 6;  // device fw version (length-prefixed ASCII)
    case 0x0080:
      return 7;  // device descriptor
    case 0x0018:
      return 32;  // available endpoints bitmap
    case 0x100C:
    case 0x100D:
    case 0x100E:
    case 0x100F:
    case 0x1010:
    case 0x1011:
    case 0x1012:
      return 13;  // weekly schedule blocks
    default:
      return 0;
  }
}

// Human-readable reason for a 0x0D response status byte (see DI_STATUS_*); "unknown" otherwise.
inline const char *status_name(uint8_t status) {
  switch (status) {
    case DI_STATUS_OK:
      return "OK";
    case DI_STATUS_NOT_FOUND_READ:
      return "attribute not found (read)";
    case DI_STATUS_NOT_FOUND_WRITE:
      return "attribute not found (write)";
    case DI_STATUS_OUT_OF_RANGE:
      return "value out of range";
    case DI_STATUS_READ_ONLY:
      return "read-only / protected";
    case DI_STATUS_LENGTH:
      return "length error";
    default:
      return "unknown";
  }
}

// Per-room thermostat product id (device_descriptor 0x0080 bytes [4:5], u16 BE).
// 0x8030 confirmed on hardware; the rest are from the product range.
inline const char *product_id_name(uint16_t pid) {
  switch (pid) {
    case 0x8020:
      return "RT24V Display (wired)";
    case 0x8021:
      return "RT24V Display+floor (wired)";
    case 0x8030:
      return "RTbattery Display (wireless)";
    case 0x8031:
      return "RTbattery Display IR (wireless)";
    case 0x8034:
      return "RTbattery Dial (wireless)";
    case 0x8035:
      return "RTbattery Dial IR (wireless)";
    default:
      return nullptr;
  }
}

inline bool product_is_wired(uint16_t pid) { return pid == 0x8020 || pid == 0x8021; }

// Result of scanning for one frame in a byte buffer.
struct FrameScan {
  bool found = false;    // a structurally complete, CRC-valid frame was found
  bool bad_crc = false;  // a complete frame was found but CRC failed
  size_t start = 0;      // offset of 0x01 sync of the (found or bad_crc) frame
  size_t len = 0;        // frame length
  size_t consumed = 0;   // bytes from the front that can be discarded (incl. junk)
};

// Scan `buf[0..n)` for the first valid frame. On `found`/`bad_crc`, `start`+`len`
// locate it and `consumed` = start+len (drop through the frame). If nothing
// complete yet, `consumed` skips leading junk up to the last partial sync so the
// caller can retain the tail and append more bytes.
inline FrameScan scan_frame(const uint8_t *buf, size_t n) {
  FrameScan r;
  size_t i = 0;
  while (i < n) {
    if (buf[i] != DI_SYNC) {
      i++;
      continue;
    }
    if (i + DI_FRAME_MIN > n) {
      r.consumed = i;  // partial frame at i; keep from here
      return r;
    }
    uint8_t len = buf[i + 1];
    if (len < DI_FRAME_MIN || len > DI_FRAME_MAX) {
      i++;  // bogus length; this wasn't a real sync
      continue;
    }
    if (i + len > n) {
      r.consumed = i;  // need more bytes to complete this frame
      return r;
    }
    if (buf[i + 2] != DI_FIXED) {
      i++;
      continue;
    }
    uint16_t crc_recv = (uint16_t) buf[i + len - 2] | ((uint16_t) buf[i + len - 1] << 8);
    uint16_t crc_calc = crc16_modbus(&buf[i], len - 2);
    if (crc_recv != crc_calc) {
      r.bad_crc = true;
      r.start = i;
      r.len = len;
      r.consumed = i + 1;  // resync one byte past this sync
      return r;
    }
    r.found = true;
    r.start = i;
    r.len = len;
    r.consumed = i + len;
    return r;
  }
  r.consumed = n;  // all junk, no sync pending
  return r;
}

}  // namespace danfoss_icon
}  // namespace esphome
