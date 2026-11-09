#pragma once
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <stddef.h>

// Dedicated EEPROM block to preserve WiFi credentials and MQTT settings.
constexpr uint16_t NET_OFFSET=640;
constexpr uint32_t NET_MAGIC=0x4E455431UL;
struct NetworkSettings {
  uint32_t magic;
  uint8_t staticEnabled;
  uint8_t ip[4], mask[4], gateway[4], dns[4];
  uint32_t check;
};
static_assert(NET_OFFSET+sizeof(NetworkSettings)<=1024, "Network EEPROM bounds");
NetworkSettings networkSettings{};
inline uint32_t networkChecksum(const NetworkSettings& c) {
  const uint8_t* p=reinterpret_cast<const uint8_t*>(&c);
  uint32_t h=2166136261UL;
  for(size_t i=0;i<offsetof(NetworkSettings,check);++i) h=(h^p[i])*16777619UL;
  return h;
}
inline void networkDefaults() {
  memset(&networkSettings,0,sizeof(networkSettings));
  networkSettings.magic=NET_MAGIC;
  networkSettings.check=networkChecksum(networkSettings);
}
inline bool networkLoad() {
  EEPROM.get(NET_OFFSET,networkSettings);
  if(networkSettings.magic!=NET_MAGIC ||
     networkSettings.check!=networkChecksum(networkSettings) ||
     networkSettings.staticEnabled>1) {
    networkDefaults();
    return false;
  }
  return true;
}
inline bool networkSave() {
  networkSettings.magic=NET_MAGIC;
  networkSettings.check=networkChecksum(networkSettings);
  EEPROM.put(NET_OFFSET,networkSettings);
  return EEPROM.commit();
}
inline IPAddress networkIp(const uint8_t a[4]) {
  return IPAddress(a[0],a[1],a[2],a[3]);
}
inline String networkStr(const uint8_t a[4]) {
  return networkIp(a).toString();
}
inline bool networkParse(const String& src,uint8_t out[4]) {
  IPAddress addr;
  if(!addr.fromString(src)) return false;
  for(uint8_t i=0;i<4;i++) out[i]=addr[i];
  return true;
}
inline bool networkValidate(const NetworkSettings& c) {
  if(!c.staticEnabled) return true;
  if(c.ip[0]==0 || c.ip[0]>=224 || c.gateway[0]==0 || c.dns[0]==0) return false;
  uint32_t m=((uint32_t)c.mask[0]<<24)|((uint32_t)c.mask[1]<<16)|
             ((uint32_t)c.mask[2]<<8)|c.mask[3];
  if(m==0 || m==0xFFFFFFFFUL) return false;
  uint32_t inverse=~m;
  if((inverse & (inverse+1))!=0) return false;
  for(uint8_t i=0;i<4;i++)
    if((c.ip[i]&c.mask[i])!=(c.gateway[i]&c.mask[i])) return false;
  if(c.ip[3]==0 || c.ip[3]==255 || memcmp(c.ip,c.gateway,4)==0) return false;
  return true;
}
inline void networkConfigureWiFi() {
  if(networkSettings.staticEnabled) {
    WiFi.config(networkIp(networkSettings.ip),
      networkIp(networkSettings.gateway),networkIp(networkSettings.mask),
      networkIp(networkSettings.dns));
  } else {
    WiFi.config(0U,0U,0U);
  }
}
