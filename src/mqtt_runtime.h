#pragma once
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "optolink.h"
#include "mqtt_settings.h"

// One whitelisted mode command topic, plus selected read-only datapoints.
// The external heating pump stays under Node-RED / vc_setMode.sh control.
// Commands must be MQTT non-retained (PubSubClient does not expose retain flag).

namespace MqttRuntime {
  static WiFiClient wifiNet;
  static PubSubClient client(wifiNet);
  static uint32_t lastReconnectMs=0;
  static uint32_t lastPollMs=0;
  static uint32_t readsOK=0;
  static uint32_t readErrors=0;
  static uint32_t published=0;
  static uint16_t nextIndex=0;
  static uint16_t lastIndex=0;
  // RAM-only display cache. Empty after restart until a fresh query succeeds.
  static String displayed[POINT_COUNT];
  static uint32_t displayedAt[POINT_COUNT]={0};
  inline void cacheValue(uint16_t index,const String& payload) {
    if(index>=POINT_COUNT) return;
    displayed[index]=payload;
    displayedAt[index]=millis();
  }
  inline uint16_t cachedCount() {
    uint16_t n=0;
    for(uint16_t i=0;i<POINT_COUNT;++i)
      if(pointSelected(i) && displayedAt[i]) ++n;
    return n;
  }
  static String lastRaw;
  static String lastError="noch keine Abfrage";
  static bool didPoll=false;
  static bool readInProgress=false;
  static volatile bool modePending=false;
  static uint8_t pendingMode=255;
  static uint32_t modeCommands=0;
  static uint32_t modeSuccess=0;
  static uint32_t modeFailures=0;
  static String lastCommand="none";
  static String lastCommandResult="none";
  inline const char* modeName(uint8_t value) {
    if(value==0x03) return "HWW";
    if(value==0x00) return "WW";
    if(value==0x05) return "OFF";
    return "UNKNOWN";
  }
  inline String resultTopic(){return String(mqttSettings.prefix)+"/setMode/result";}
  inline String commandTopic(){return String(mqttSettings.prefix)+"/setMode";}
  inline void publishResult(const String& result,const String& text,uint8_t actual=255) {
    lastCommandResult=result;
    if(!client.connected()) return;
    String s="{\"result\":\""+result+"\",\"reason\":\""+text+"\"";
    if(actual!=255) s+=",\"actual\":\""+String(modeName(actual))+"\"";
    s+="}";
    client.publish(resultTopic().c_str(),s.c_str(),false);
  }
  inline void commandCallback(char* topic,byte* payload,unsigned int length) {
    if(String(topic)!=commandTopic() || length==0 || length>8) return;
    char input[9];
    memcpy(input,payload,length); input[length]=0;
    const String mode(input);
    lastCommand=mode;
    ++modeCommands;
    if(!mqttSettings.optoEnabled) {
      publishResult("REJECTED","OPTO_DISABLED");
      return;
    }
    if(modePending) {
      publishResult("REJECTED","BUSY");
      return;
    }
    if(mode=="HWW") pendingMode=3;
    else if(mode=="WW") pendingMode=0;
    else if(mode=="OFF") pendingMode=5;
    else {
      publishResult("REJECTED","INVALID_MODE");
      return;
    }
    modePending=true;
  }

  inline String pointTopic(const char* name) {
    return String(mqttSettings.prefix)+"/"+name;
  }
  inline void stop() {
    client.disconnect();
    lastReconnectMs=0;
  }
  inline void begin() {
    client.setCallback(commandCallback);
    client.setServer(mqttSettings.host,mqttSettings.port);
    client.setBufferSize(384);
    client.setKeepAlive(30);
    lastReconnectMs=0;
    lastPollMs=millis();
  }
  inline bool readPoint(const DataPoint& p, uint8_t* dst, uint8_t& received) {
    received=0;
    auto& uart=Optolink::rx;
    while(uart.available()) (void)uart.read();
    uart.write(static_cast<uint8_t>(0x04));
    uart.flush();
    bool ack=false;
    uint32_t start=millis();
    while(millis()-start<2700) {
      if(uart.available() && uart.read()==0x05) { ack=true; break; }
      yield();
    }
    if(!ack) {lastError="SYNC_TIMEOUT";return false;}
    const uint8_t req[5]={0x01,0xF7,static_cast<uint8_t>(p.addr>>8),
                           static_cast<uint8_t>(p.addr&0xFF),p.length};
    uart.write(req,sizeof(req));
    uart.flush();
    start=millis();
    while(received<p.length && millis()-start<900) {
      if(uart.available()) dst[received++]=static_cast<uint8_t>(uart.read());
      else yield();
    }
    if(received!=p.length) {lastError="DATA_TIMEOUT";return false;}
    return true;
  }
  inline String rawHex(const uint8_t* bytes, uint8_t len) {
    String s;
    for(uint8_t i=0;i<len;++i) {
      char b[4];
      snprintf(b,sizeof(b),"%02X",bytes[i]);
      if(i) s+=' ';
      s+=b;
    }
    return s;
  }
  inline String valueJson(const DataPoint& p, const uint8_t* bytes,
                          uint8_t n, bool invalid) {
    String s="{\"valid\":";
    s+=invalid?"false":"true";
    s+=",\"raw\":\""+rawHex(bytes,n)+"\"";
    if(!invalid) {
      if(p.decoder==1 && n==2) {
        int16_t v=static_cast<int16_t>((uint16_t)bytes[0]|((uint16_t)bytes[1]<<8));
        s+=",\"value\":"+String(v/10.0f,1)+",\"unit\":\"C\"";
      } else if(p.decoder==2 && n==1) {
        s+=",\"value\":"+String(bytes[0])+",\"unit\":\"C\"";
      } else if(p.decoder==3 && n==1) {
        const char *names[]={"WW","RED","NORM","H+WW","H+WW_FS","ABSCHALT"};
        s+=",\"value\":";
        s+=String(bytes[0]);
        if(bytes[0]<6) s+=",\"mode\":\""+String(names[bytes[0]])+"\"";
      }
    } else s+=",\"reason\":\"all_ff\"";
    return s+"}";
  }
  inline void setMode() {
    if(!modePending) return;
    const uint8_t wanted=pendingMode;
    modePending=false;
    if(!mqttSettings.optoEnabled || !client.connected()) {
      ++modeFailures;
      publishResult("REJECTED","NOT_READY");
      return;
    }
    const DataPoint modePoint={"getBetriebArtM1","Betriebsart M1",0x2301,1,4,3};
    uint8_t current[9]={0},len=0;
    if(!readPoint(modePoint,current,len) || len!=1 || current[0]==0xFF) {
      ++modeFailures;
      publishResult("ERROR","READ_BEFORE_WRITE_FAILED");
      return;
    }
    if(current[0]==wanted) {
      ++modeSuccess;
      publishResult("OK","ALREADY_SET",current[0]);
      return;
    }
    auto& uart=Optolink::rx;
    while(uart.available()) (void)uart.read();
    uart.write(static_cast<uint8_t>(0x04));
    uart.flush();
    bool ack=false;
    uint32_t started=millis();
    while(millis()-started<2700) {
      if(uart.available() && uart.read()==0x05) {ack=true;break;}
      yield();
    }
    if(!ack){
      ++modeFailures;
      publishResult("ERROR","WRITE_SYNC_TIMEOUT");
      return;
    }
    const uint8_t msg[]={0x01,0xF4,0x23,0x01,0x01,wanted};
    uart.write(msg,sizeof(msg));
    uart.flush();
    uint8_t response=255;
    started=millis();
    while(millis()-started<900) {
      if(uart.available()) {response=static_cast<uint8_t>(uart.read());break;}
      yield();
    }
    if(response==255) {
      // The status byte could be missing, but read-back is authoritative.
      lastError="WRITE_STATUS_MISSING";
    }
    // Do not assume the write status alone confirms the plant mode.
    delay(150);
    uint8_t check[9]={0},readLen=0;
    if(!readPoint(modePoint,check,readLen) || readLen!=1) {
      ++modeFailures;
      publishResult("ERROR","READBACK_FAILED");
      return;
    }
    if(check[0]!=wanted) {
      ++modeFailures;
      publishResult("ERROR","READBACK_MISMATCH",check[0]);
      return;
    }
    ++modeSuccess;
    publishResult("OK","VERIFIED",check[0]);
    const String measurement=valueJson(modePoint,check,1,false);
    for(uint16_t i=0;i<POINT_COUNT;++i)
      if(String(POINTS[i].name)=="getBetriebArtM1") {cacheValue(i,measurement);break;}
    if(client.connected())
      client.publish(pointTopic("getBetriebArtM1").c_str(),measurement.c_str(),true);
  }
  inline void onePoll() {
    if(!mqttSettings.optoEnabled || !mqttSettings.mqttEnabled ||
       !client.connected() || !selectionCount()) return;
    uint16_t chosen=POINT_COUNT;
    for(uint16_t k=0;k<POINT_COUNT;++k) {
      const uint16_t i=(nextIndex+k)%POINT_COUNT;
      if(pointSelected(i)){chosen=i;break;}
    }
    if(chosen>=POINT_COUNT) return;
    nextIndex=(chosen+1)%POINT_COUNT;
    lastIndex=chosen;
    didPoll=true;
    readInProgress=true;
    uint8_t data[9]={0},len=0;
    const auto& p=POINTS[chosen];
    bool ok=readPoint(p,data,len);
    readInProgress=false;
    if(!ok) {++readErrors;return;}
    ++readsOK;
    lastRaw=rawHex(data,len);
    bool invalid=true;
    for(uint8_t i=0;i<len;i++) if(data[i]!=0xFF) invalid=false;
    String payload=valueJson(p,data,len,invalid);
    cacheValue(chosen,payload);
    if(client.publish(pointTopic(p.name).c_str(),payload.c_str(),true)) {
      ++published;
      lastError=invalid?"ALL_FF":"OK";
    } else lastError="MQTT_PUBLISH_FAILED";
  }
  inline void tick() {
    if(WiFi.status()!=WL_CONNECTED || !mqttSettings.mqttEnabled ||
       !mqttSettings.host[0]) {
      if(client.connected()) client.disconnect();
      return;
    }
    if(!client.connected()) {
      if(lastReconnectMs && millis()-lastReconnectMs<10000) return;
      lastReconnectMs=millis();
      client.setServer(mqttSettings.host,mqttSettings.port);
      String clientId="vito-"+String(ESP.getChipId(),HEX);
      String statusTopic=String(mqttSettings.prefix)+"/status";
      bool connected=false;
      if(mqttSettings.user[0]) {
        connected=client.connect(clientId.c_str(),mqttSettings.user,
          mqttSettings.password,statusTopic.c_str(),1,true,"offline");
      } else {
        connected=client.connect(clientId.c_str(),statusTopic.c_str(),1,true,"offline");
      }
      if(connected) {
        client.publish(statusTopic.c_str(),"online",true);
        client.subscribe(commandTopic().c_str(),0);
      }
      return;
    }
    client.loop();
    if(modePending) {
      setMode();
      client.loop();
    }
    if(mqttSettings.optoEnabled && !readInProgress &&
       millis()-lastPollMs >= (uint32_t)mqttSettings.intervalSec*1000UL) {
      lastPollMs=millis();
      onePoll();
    }
  }
}
