// Host-side sanity test for protocol.h against known-good reference frames.
// Build & run:  c++ -std=c++17 -o /tmp/dtest test_protocol.cpp && /tmp/dtest
#include "protocol.h"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace esphome::danfoss_icon;

static int fails = 0;
static void expect(bool ok, const char *what) {
  printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
  if (!ok)
    fails++;
}

static bool eq(const uint8_t *a, const std::vector<uint8_t> &b, size_t n) {
  return n == b.size() && memcmp(a, b.data(), n) == 0;
}

int main() {
  uint8_t buf[64];

  // READ Room1 (idx 0x31) attr 0x0300 -> 01 0a 01 0c 31 10 03 00 [crc]
  {
    uint16_t ids[] = {0x0300};
    size_t n = build_read(buf, sizeof(buf), 0x31, ids, 1);
    expect(eq(buf, {0x01, 0x0a, 0x01, 0x0c, 0x31, 0x10, 0x03, 0x00, 0x35, 0xde}, n),
           "build_read(0x31, 0x0300) matches reference frame");
  }

  // WRITE Room1 setpoint 0x0509 = 0x0866 -> 01 0c 01 0c 31 01 05 09 08 66 [crc]
  {
    uint8_t val[] = {0x08, 0x66};
    size_t n = build_write(buf, sizeof(buf), 0x31, 0x0509, val, 2);
    bool prefix_ok =
        (n == 12) && eq(buf, {0x01, 0x0c, 0x01, 0x0c, 0x31, 0x01, 0x05, 0x09, 0x08, 0x66, buf[10], buf[11]}, n);
    uint16_t crc = crc16_modbus(buf, 10);
    expect(prefix_ok && buf[10] == (crc & 0xFF) && buf[11] == (crc >> 8),
           "build_write(0x31, 0x0509=0x0866) prefix + valid CRC");
  }

  // PARSE the controller's read reply: 01 09 01 0d 00 09 8a b3 c2  (0x098a = 24.42C)
  {
    std::vector<uint8_t> reply = {0x01, 0x09, 0x01, 0x0d, 0x00, 0x09, 0x8a, 0xb3, 0xc2};
    FrameScan r = scan_frame(reply.data(), reply.size());
    bool ok = r.found && r.start == 0 && r.len == 9 && r.consumed == 9;
    const uint8_t *f = reply.data() + r.start;
    ok = ok && f[3] == DI_CMD_RESPONSE && f[4] == DI_STATUS_OK;
    uint16_t v = ((uint16_t) f[5] << 8) | f[6];  // value bytes, BE
    expect(ok && v == 0x098a, "scan_frame parses 0x0D reply, value 0x098a (24.42C)");
  }

  // Resync past leading junk + a bad-CRC frame, then find the good one.
  {
    std::vector<uint8_t> s = {0xff, 0xaa,                                             // junk
                              0x01, 0x09, 0x01, 0x0d, 0x00, 0x09, 0x8a, 0x00, 0x00,   // bad crc
                              0x01, 0x09, 0x01, 0x0d, 0x00, 0x09, 0x8a, 0xb3, 0xc2};  // good
    FrameScan r1 = scan_frame(s.data(), s.size());
    expect(r1.bad_crc && r1.start == 2, "scan_frame flags bad CRC at first frame");
    size_t off = r1.consumed;
    FrameScan r2 = scan_frame(s.data() + off, s.size() - off);
    expect(r2.found, "scan_frame recovers the good frame after a bad one");
  }

  printf(fails ? "\n%d FAIL(s)\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
