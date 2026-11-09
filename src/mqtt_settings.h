#pragma once
#include <Arduino.h>
#include <EEPROM.h>
#include <stddef.h>
#include "mqtt_catalog.h"

// Separate EEPROM record. The existing Wi-Fi/OTA Settings record at offset 0
// remains byte-for-byte compatible with v0.3.2.
constexpr size_t MQTT_OFFSET = 256;
constexpr uint32_t MQTT_MAGIC = 0x4D515431UL;
constexpr size_t SELECT_BYTES = (POINT_COUNT+7)/8;
struct MqttSettings {
  uint32_t magic;
  char host[65];
  uint16_t port;
  char user[33];
  char password[65];
  char prefix[65];
  uint16_t intervalSec;
  uint8_t mqttEnabled;
  uint8_t optoEnabled;
  uint8_t selection[SELECT_BYTES];
  uint32_t check;
};
static_assert(MQTT_OFFSET + sizeof(MqttSettings) <= 1024, "EEPROM size too small");
MqttSettings mqttSettings{};

inline uint32_t mqttChecksum(const MqttSettings& s) {
  uint32_t h=2166136261UL;
  const uint8_t *p=reinterpret_cast<const uint8_t*>(&s);
  for (size_t i=0;i<offsetof(MqttSettings,check);++i)
    h=(h^p[i])*16777619UL;
  return h;
}
inline bool pointSelected(uint16_t n) {
  return n<POINT_COUNT && (mqttSettings.selection[n/8] & (1U<<(n%8)));
}
inline uint16_t selectionCount() {
  uint16_t c=0;
  for(uint16_t i=0;i<POINT_COUNT;++i) if(pointSelected(i)) ++c;
  return c;
}
inline void mqttSettingsDefaults() {
  memset(&mqttSettings,0,sizeof(mqttSettings));
  mqttSettings.magic=MQTT_MAGIC;
  mqttSettings.port=1883;
  mqttSettings.intervalSec=15;
  strncpy(mqttSettings.prefix,"vito/kw2",sizeof(mqttSettings.prefix)-1);
  // Defaults are just proposed choices; both transmission and polling stay OFF.
  for(uint16_t i=0;i<POINT_COUNT;++i) {
    String n=POINTS[i].name;
    if(n=="getTempA" || n=="getTempKist" || n=="getTempKsoll" ||
       n=="getTempWWist" || n=="getTempWWsoll" || n=="getBetriebArtM1" ||
       n=="getBetriebArtM2" || n=="getBrennerStufe" || n=="getBrennerStarts" ||
       n=="getStatusStoerung" || n=="getPumpeStatusM1" ||
       n=="getPumpeStatusSp")
      mqttSettings.selection[i/8] |= 1U<<(i%8);
  }
  mqttSettings.check=mqttChecksum(mqttSettings);
}
inline bool mqttLoadSettings() {
  EEPROM.get(MQTT_OFFSET,mqttSettings);
  if(mqttSettings.magic!=MQTT_MAGIC || mqttSettings.check!=mqttChecksum(mqttSettings) ||
     !memchr(mqttSettings.host,0,sizeof(mqttSettings.host)) ||
     !memchr(mqttSettings.user,0,sizeof(mqttSettings.user)) ||
     !memchr(mqttSettings.password,0,sizeof(mqttSettings.password)) ||
     !memchr(mqttSettings.prefix,0,sizeof(mqttSettings.prefix)) ||
     mqttSettings.port==0 || mqttSettings.intervalSec<3 ||
     mqttSettings.intervalSec>3600) {
    mqttSettingsDefaults();
    return false;
  }
  return true;
}
inline bool mqttSaveSettings() {
  mqttSettings.magic=MQTT_MAGIC;
  mqttSettings.check=mqttChecksum(mqttSettings);
  EEPROM.put(MQTT_OFFSET,mqttSettings);
  return EEPROM.commit();
}
