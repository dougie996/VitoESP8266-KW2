#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>

// v0.3.2 KW2: SoftwareSerial RX D5/GPIO14, TX D6/GPIO12.
// 4800 baud, eight data bits, even parity, two stop bits.
// Only read Kesseltemperatur 0x0802, length two bytes.
namespace Optolink {
  constexpr uint32_t POLL_INTERVAL_MS = 15000;
  constexpr uint32_t SYNC_TIMEOUT_MS = 5000;
  constexpr uint32_t DATA_TIMEOUT_MS = 1400;

  static SoftwareSerial rx;
  static bool active = false;

  struct State {
    bool valid = false;
    float boilerC = 0.0f;
    uint32_t success = 0;
    uint32_t errors = 0;
    uint32_t rxBytes = 0;
    uint32_t rxDiscarded = 0;
    uint32_t rxSyncBytes = 0;
    uint32_t rxDataBytes = 0;
    uint32_t syncAckCount = 0;
    uint32_t txSyncCount = 0;
    uint32_t txRequestCount = 0;
    uint32_t rxAfterRequestCount = 0;
    uint32_t lastSyncWaitMs = 0;
    String lastResponseHex;
    uint32_t lastSuccessMs = 0;
    uint32_t lastAttemptMs = 0;
    String recentHex;
    const char* detail = "noch keine Abfrage";
  };
  static State state;

  inline void begin() {
    if (active) return;
    // Both optical pins are dedicated GPIOs, no hardware UART output.
    Serial.end();
    rx.begin(4800, SWSERIAL_8E2, 14, 12, false, 128, 256);
    active = true;
  }

  inline void end() {
    if (!active) return;
    rx.end();
    active = false;
  }

  inline uint8_t getByte(uint8_t phase) {
    const uint8_t b = static_cast<uint8_t>(rx.read());
    state.rxBytes++;
    if (phase == 0) state.rxDiscarded++;
    else if (phase == 1) state.rxSyncBytes++;
    else state.rxDataBytes++;
    if (phase == 1 && b == 0x05) ++state.syncAckCount;
    char str[4];
    snprintf(str, sizeof(str), "%02X ", b);
    state.recentHex += str;
    if (state.recentHex.length() > 90)
      state.recentHex.remove(0, 3);
    return b;
  }

  inline bool readUntil(uint8_t& b, uint32_t timeoutMs) {
    const uint32_t start = millis();
    while (millis() - start < timeoutMs) {
      if (rx.available()) {
        b = getByte(2);
        ++state.rxAfterRequestCount;
        char out[4];
        snprintf(out, sizeof(out), "%02X ", b);
        state.lastResponseHex += out;
        return true;
      }
      yield();
    }
    return false;
  }

  inline void poll() {
    if (!active) return;
    state.lastAttemptMs = millis();
    state.lastResponseHex = "";
    while (rx.available()) getByte(0);

    // KW2 SYNC = SEND 04; WAIT 05. ACK may be sent periodically.
    rx.write(static_cast<uint8_t>(0x04));
    rx.flush();
    ++state.txSyncCount;
    bool synced = false;
    const uint32_t start = millis();
    while (millis() - start < SYNC_TIMEOUT_MS) {
      if (rx.available()) {
        const uint8_t b = getByte(1);
        if (b == 0x05) {
          synced = true;
          break;
        }
      }
      yield();
    }
    state.lastSyncWaitMs = millis() - start;
    if (!synced) {
      state.errors++;
      state.detail = (state.rxBytes == 0) ?
          "SYNC: keine UART-Bytes auf D5" :
          "SYNC: UART-Bytes gesehen, ACK 05 fehlt";
      return;
    }

    // KW2 GETADDR: SEND 01 F7 [addr-high] [addr-low] [length].
    const uint8_t request[] = {0x01, 0xF7, 0x08, 0x02, 0x02};
    rx.write(request, sizeof(request));
    rx.flush();
    ++state.txRequestCount;
    uint8_t low = 0, high = 0;
    if (!readUntil(low, DATA_TIMEOUT_MS) ||
        !readUntil(high, DATA_TIMEOUT_MS)) {
      state.errors++;
      state.detail = "READ: Antwort unvollstaendig";
      return;
    }
    const int16_t raw = static_cast<int16_t>(
        static_cast<uint16_t>(low) |
        (static_cast<uint16_t>(high) << 8));
    if (raw == -1 || raw == 32767 || raw < -500 || raw > 1500) {
      state.errors++;
      state.detail = "READ: Temperaturwert ungueltig";
      return;
    }
    state.boilerC = raw / 10.0f;
    state.valid = true;
    state.success++;
    state.lastSuccessMs = millis();
    state.detail = "OK";
  }
}
