// VitoESP8266 v0.5.3 - KW2 SoftwareSerial TX(D6/GPIO12) RX(D5/GPIO14), 4800 8E2.
// Hardware: Wemos D1 mini / ESP8266; beide Optolink-Signale an SoftwareSerial.
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include "optolink.h"
#include "mqtt_settings.h"
#include "network_settings.h"
#include "mqtt_runtime.h"

constexpr uint32_t MAGIC = 0x56495431UL;
constexpr uint16_t EEPROM_SIZE = 1024;
#include "local_secrets.h" // local-only, excluded from Git
constexpr unsigned long WIFI_TIMEOUT_MS = 18000;
constexpr unsigned long WIFI_RETRY_MS = 30000;

struct Settings {
  uint32_t magic;
  char ssid[33];
  char wifiPassword[65];
  char otaPassword[65];
  uint32_t check;
};
Settings settings{};
ESP8266WebServer web(80);
String hostname;
bool setupMode = false;
bool otaReady = false;
unsigned long lastWifiRetry = 0;
bool otaInProgress = false;

uint32_t checksum(const Settings& s) {
  uint32_t h = 2166136261UL;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
  for (size_t i = 0; i < offsetof(Settings, check); ++i) {
    h = (h ^ p[i]) * 16777619UL;
  }
  return h;
}

bool loadSettings() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, settings);
  if (settings.magic != MAGIC || settings.check != checksum(settings)) return false;
  if (!memchr(settings.ssid, '\0', sizeof(settings.ssid))) return false;
  if (!memchr(settings.wifiPassword, '\0', sizeof(settings.wifiPassword))) return false;
  if (!memchr(settings.otaPassword, '\0', sizeof(settings.otaPassword))) return false;
  return strlen(settings.ssid) > 0 && strlen(settings.otaPassword) >= 12;
}

String escapeHtml(String str) {
  str.replace("&", "&amp;"); str.replace("<", "&lt;");
  str.replace(">", "&gt;"); str.replace("\"", "&quot;");
  str.replace("'", "&#39;");
  return str;
}

String htmlPage(const String& body) {
  return String(F("<!doctype html><html lang='de'><head><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>VitoESP8266</title><style>body{font:16px system-ui;max-width:650px;"
                  "margin:35px auto;padding:0 16px;line-height:1.6}input{display:block;width:100%;"
                  "box-sizing:border-box;padding:10px;margin:8px 0 16px}button{padding:10px 20px}"
                  "code{overflow-wrap:anywhere}</style></head><body>")) + body + F("</body></html>");
}

void startSetupAP() {
  setupMode = true;
  WiFi.mode(WIFI_AP_STA);
  String ap = "VitoESP-Setup-" + String(ESP.getChipId(), HEX);
  WiFi.softAP(ap.c_str(), SETUP_PASSWORD);
}

void startOTA() {
  if (otaReady || WiFi.status() != WL_CONNECTED) return;
  ArduinoOTA.setHostname(hostname.c_str());
  ArduinoOTA.setPassword(settings.otaPassword);
  ArduinoOTA.onStart([]() { otaInProgress = true; MqttRuntime::stop(); Optolink::end(); });
  ArduinoOTA.onEnd([]() { otaInProgress = false; });
  ArduinoOTA.onError([](ota_error_t) { otaInProgress = false; Optolink::begin(); MqttRuntime::begin(); });
  ArduinoOTA.begin();
  otaReady = true;
  MDNS.begin(hostname.c_str());
}


// Human-readable web display only; MQTT payloads remain unchanged.
String readableBurnerOilValue(const char* name, const String& raw) {
  if (raw.length()!=11) return String();
  unsigned int b0=0,b1=0,b2=0,b3=0;
  if (sscanf(raw.c_str(),"%2x %2x %2x %2x",&b0,&b1,&b2,&b3)!=4)
    return String();
  const uint32_t n=static_cast<uint32_t>(b0) |
      (static_cast<uint32_t>(b1)<<8) |
      (static_cast<uint32_t>(b2)<<16) |
      (static_cast<uint32_t>(b3)<<24);
  String text;
  const String pointName(name);
  if (pointName=="getBrennerStunden1") {
    text=String(n/3600.0,1)+" h";
  } else if (pointName=="getOeldurchsatz") {
    text=String(n/10.0,1)+" l/h";
  } else if (pointName=="getOelverbrauch") {
    text=String(n/1000.0,2)+" l";
  } else {
    return String();
  }
  text.replace(".",",");
  return text;
}

void handleRoot() {
  if (setupMode) {
    String s = F("<h1>VitoESP8266 einrichten</h1><p>Nur 2,4-GHz-WLAN. "
                 "Nach dem Speichern startet das Geraet neu.</p>"
                 "<form action='/save' method='POST'>"
                 "<label>WLAN-Name (SSID)</label><input name='ssid' maxlength='32' required>"
                 "<label>WLAN-Passwort</label><input name='wifi' type='password' maxlength='64'>"
                 "<label>Neues OTA-Passwort (mindestens 12 Zeichen)</label>"
                 "<input name='ota' type='password' minlength='12' maxlength='64' required>"
                 "<button type='submit'>Speichern und Neustarten</button></form>");
    web.send(200, "text/html; charset=utf-8", htmlPage(s));
    return;
  }
  String s = F("<h1>VitoESP8266 v0.5.3</h1><p>Vitotronic 200 KW2, MQTT (nur lesend)</p>");
  s += "<p>Hostname: <code>"+escapeHtml(hostname)+"</code> - IP: "+WiFi.localIP().toString()+"</p>";
  s += "<p>WLAN: "+String(WiFi.RSSI())+" dBm</p>";
  s += "<p>Broker: "+(mqttSettings.host[0]?escapeHtml(String(mqttSettings.host)):String("nicht konfiguriert"));
  s += " / MQTT: "+String(MqttRuntime::client.connected()?"verbunden":"getrennt")+"</p>";
  s += "<p>Optolink: "+String(mqttSettings.optoEnabled?"Abfragen aktiviert":"Abfragen deaktiviert")+"</p>";
  s += "<p>Ausgewaehlte Datenpunkte: "+String(selectionCount())+" / "+String(POINT_COUNT)+"</p>";
  s += "<p>Erfolgreiche Optolink-Abfragen: "+String(MqttRuntime::readsOK)+
       ", Fehler: "+String(MqttRuntime::readErrors)+
       ", MQTT-Nachrichten: "+String(MqttRuntime::published)+"</p>";
  s += F("<p><a href='/config'>MQTT & Datenpunkte konfigurieren</a> | "
         "<a href='/network'>Netzwerk konfigurieren</a></p>");
  s += F("<p><a href='/status'>Diagnose als JSON</a></p>");
  s += "<p>Netzwerk: "+String(networkSettings.staticEnabled?"Statisch":"DHCP")+"</p>";
  s += "<h2>Ausgewaehlte Heizungswerte</h2><p>Empfangen: "+
       String(MqttRuntime::cachedCount())+" / "+String(selectionCount())+
       ". Automatische Aktualisierung alle 20 Sekunden.</p>";
  s += F("<meta http-equiv='refresh' content='20'>");
  s += F("<div style='overflow-x:auto'><table style='border-collapse:collapse;width:100%'>"
         "<tr style='background:#e9eef4'><th style='text-align:left;padding:8px'>Datenpunkt</th>"
         "<th style='text-align:left;padding:8px'>Wert</th>"
         "<th style='text-align:left;padding:8px'>Status</th></tr>");
  for(uint16_t i=0;i<POINT_COUNT;++i) {
    if(!pointSelected(i)) continue;
    String value="Noch kein Messwert",status="Wartet",raw;
    const String& json=MqttRuntime::displayed[i];
    if(MqttRuntime::displayedAt[i]) {
      const bool valid=json.indexOf("\"valid\":true")>=0;
      status=valid?"OK":"Ungueltig";
      int key=json.indexOf("\"mode\":\"");
      if(key>=0) {
        key+=8;int finish=json.indexOf('"',key);
        if(finish>key) value=json.substring(key,finish);
      } else {
        key=json.indexOf("\"value\":");
        if(key>=0) {
          key+=8;int finish=json.indexOf(',',key);
          if(finish<0) finish=json.indexOf('}',key);
          value=json.substring(key,finish);
          if(json.indexOf("\"unit\":\"C\"")>=0) value+=" &deg;C";
        } else {
          key=json.indexOf("\"raw\":\"");
          if(key>=0) {
            key+=7;int finish=json.indexOf('"',key);
            if(finish>key) {
              raw=json.substring(key,finish);
              String readable=valid?readableBurnerOilValue(POINTS[i].name,raw):String();
              value=readable.length()?readable:"0x"+raw;
            }
          }
        }
      }
      if(millis()-MqttRuntime::displayedAt[i] >
         (uint32_t)mqttSettings.intervalSec * max((uint16_t)1,selectionCount()) * 3000UL)
         status="Veraltet";
    }
    s+="<tr style='border-bottom:1px solid #ddd'><td style='padding:8px'>"+
      escapeHtml(String(POINTS[i].label))+
      "<br><small style='color:#657184'>"+String(POINTS[i].name)+
      "</small></td><td style='padding:8px'>"+value+
      "</td><td style='padding:8px'>"+status+"</td></tr>";
  }
  s+=F("</table></div>");
  web.send(200, "text/html; charset=utf-8", htmlPage(s));
}

void handleStatus() {
  String json = "{\"version\":\"0.5.3\",\"hostname\":\""+hostname+"\",";
  json += "\"wifi_connected\":"+String(WiFi.status()==WL_CONNECTED?"true":"false")+",";
  json += "\"ip\":\""+WiFi.localIP().toString()+"\",";
  json += "\"rssi_dbm\":"+String(WiFi.RSSI())+",";
  json += "\"uptime_sec\":"+String(millis()/1000)+",";
  json += "\"free_heap\":"+String(ESP.getFreeHeap())+",";
  json += "\"mqtt_enabled\":"+String(mqttSettings.mqttEnabled?"true":"false")+",";
  json += "\"mqtt_connected\":"+String(MqttRuntime::client.connected()?"true":"false")+",";
  json += "\"mqtt_host\":\""+String(mqttSettings.host)+"\",";
  json += "\"mqtt_port\":"+String(mqttSettings.port)+",";
  json += "\"mqtt_prefix\":\""+String(mqttSettings.prefix)+"\",";
  json += "\"opto_enabled\":"+String(mqttSettings.optoEnabled?"true":"false")+",";
  json += "\"selected_count\":"+String(selectionCount())+",";
  json += "\"poll_interval_sec\":"+String(mqttSettings.intervalSec)+",";
  json += "\"opto_success\":"+String(MqttRuntime::readsOK)+",";
  json += "\"opto_errors\":"+String(MqttRuntime::readErrors)+",";
  json += "\"mqtt_published\":"+String(MqttRuntime::published)+",";
  json += "\"last_error\":\""+MqttRuntime::lastError+"\",";
  json += "\"last_point\":\""+String(MqttRuntime::didPoll?POINTS[MqttRuntime::lastIndex].name:"")+"\",";
  json += "\"last_raw\":\""+MqttRuntime::lastRaw+"\",";
  json += "\"mode_commands\":"+String(MqttRuntime::modeCommands)+",";
  json += "\"mode_success\":"+String(MqttRuntime::modeSuccess)+",";
  json += "\"mode_failures\":"+String(MqttRuntime::modeFailures)+",";
  json += "\"last_mode_command\":\""+MqttRuntime::lastCommand+"\",";
  json += "\"last_mode_result\":\""+MqttRuntime::lastCommandResult+"\",";
  json += "\"ip_mode\":\""+String(networkSettings.staticEnabled?"static":"dhcp")+"\",";
  json += "\"cached_count\":"+String(MqttRuntime::cachedCount())+",";
  json += "\"rx_gpio\":14,\"tx_gpio\":12}";
  web.send(200,"application/json; charset=utf-8",json);
}

void handleConfig() {
  web.setContentLength(CONTENT_LENGTH_UNKNOWN);
  web.send(200, "text/html; charset=utf-8", "");
  web.sendContent(F("<!doctype html><html lang='de'><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>VitoESP MQTT Konfiguration</title><style>"
      "body{font:15px system-ui;margin:24px auto;padding:0 18px;max-width:980px;line-height:1.5;"
      "background:#fafafa;color:#18202c}h1{font-size:26px}"
      "fieldset{border:1px solid #cbd5e1;border-radius:10px;background:white;margin:16px 0;padding:18px}"
      "legend{font-weight:700}input[type=text],input[type=password],input[type=number]{"
      "padding:8px;border:1px solid #94a3b8;border-radius:5px;box-sizing:border-box;max-width:100%;width:350px}"
      "label.field{display:block;margin:10px 0}label.check{display:block;padding:5px 0;break-inside:avoid}"
      "label.check input{margin-right:9px}.cols{columns:2;column-gap:26px}"
      "details{margin:12px 0;border:1px solid #ddd;border-radius:6px;padding:10px}"
      "summary{font-weight:650;cursor:pointer}button{background:#164e85;color:#fff;border:0;border-radius:6px;"
      "padding:12px 22px;font-weight:700;cursor:pointer}.muted{color:#64748b}"
      "@media(max-width:660px){.cols{columns:1}}</style></head><body>"
      "<p><a href='/'>Startseite</a> | <a href='/status'>Status JSON</a></p>"
      "<h1>MQTT und Datenpunkte</h1><p>VitoESP8266 v0.5.3 - MQTT-Lesewerte und Betriebsart M1.</p>"
      "<form method='POST' action='/config'>"
      "<fieldset><legend>MQTT Broker</legend>"));
  String s;
  s="<label class='field'>Broker-Adresse (IP oder Hostname)<br><input type='text' name='host' maxlength='64' value='"+
     escapeHtml(String(mqttSettings.host))+"' placeholder='192.168.3.100'></label>";
  s+="<label class='field'>Port<br><input type='number' name='port' min='1' max='65535' value='"+String(mqttSettings.port)+"'></label>";
  s+="<label class='field'>Benutzername (optional)<br><input type='text' name='user' maxlength='32' value='"+
      escapeHtml(String(mqttSettings.user))+"'></label>";
  s+="<label class='field'>Passwort (leer lassen = unveraendert)<br><input type='password' name='password' maxlength='64' autocomplete='new-password'></label>";
  s+="<label class='field'>MQTT Topic-Praefix<br><input type='text' name='prefix' maxlength='64' value='"+
      escapeHtml(String(mqttSettings.prefix))+"'></label>";
  s+="<label class='field'>Sekunden zwischen Datenpunkt-Abfragen<br><input type='number' name='interval' min='3' max='3600' value='"+
      String(mqttSettings.intervalSec)+"'></label>";
  s+="<label class='check'><input type='checkbox' name='mqttEnabled' value='1' "+
      String(mqttSettings.mqttEnabled?"checked":"")+">MQTT-Verbindung aktivieren</label>";
  s+="<label class='check'><input type='checkbox' name='optoEnabled' value='1' "+
      String(mqttSettings.optoEnabled?"checked":"")+">Optolink-Abfragen aktivieren (nur mit montiertem Optokopf)</label>";
  s+="<p class='muted'>Ein Wert je Abfrageintervall. Topics: &lt;Praefix&gt;/&lt;Befehlsname&gt;."
     " Betriebsarten per MQTT: <code>HWW</code>, <code>WW</code>, <code>OFF</code> an "
     "&lt;Praefix&gt;/setMode, Antwort unter &lt;Praefix&gt;/setMode/result. "
     "Befehle nur ohne Retain versenden. Die externe Heizkreispumpe steuert Node-RED. "
     "Ohne Optokopf werden keine Befehle ausgefuehrt.</p>";
  web.sendContent(s);
  web.sendContent(F("</fieldset><fieldset><legend>Datenpunkte fuer MQTT</legend>"
     "<p>Alle 127 lokal dokumentierten Lesebefehle. Es koennen mehrere ausgewaehlt werden."
     " Einige Adressen lieferten beim Verfuegbarkeitstest ausschliesslich FF.</p>"
     "<p><button type='button' onclick=\"document.querySelectorAll('.check-point').forEach(x=>x.checked=true)\">Alle waehlen</button> "
     "<button type='button' onclick=\"document.querySelectorAll('.check-point').forEach(x=>x.checked=false)\">Keine waehlen</button></p>"));
  for(uint8_t group=0;group<7;++group) {
    uint16_t count=0;
    for(uint16_t i=0;i<POINT_COUNT;++i) if(POINTS[i].group==group) ++count;
    s="<details";
    if(group==0) s+=" open";
    s+="><summary>"+escapeHtml(String(GROUP_NAMES[group]))+" ("+String(count)+")</summary><div class='cols'>";
    web.sendContent(s);
    for(uint16_t i=0;i<POINT_COUNT;++i) {
      const DataPoint& dp=POINTS[i];
      if(dp.group!=group) continue;
      char addr[5];snprintf(addr,sizeof(addr),"%04X",dp.addr);
      s="<label class='check'><input class='check-point' type='checkbox' name='p"+String(i)+
        "' value='1' "+String(pointSelected(i)?"checked":"")+">"+
        escapeHtml(String(dp.label))+" <span class='muted'>("+String(dp.name)+
        ", 0x"+String(addr)+")</span></label>";
      web.sendContent(s);
    }
    web.sendContent(F("</div></details>"));
    yield();
  }
  web.sendContent(F("</fieldset><p><button type='submit'>Einstellungen speichern</button></p>"
    "<p class='muted'>Nur im vertrauenswuerdigen Heimnetz benutzen. Zugang derzeit nicht passwortgeschuetzt.</p>"
    "</form></body></html>"));
  web.sendContent("");
}

void handleConfigSave() {
  String host=web.arg("host"),user=web.arg("user"),prefix=web.arg("prefix");
  String pw=web.arg("password");
  long port=web.arg("port").toInt(),interval=web.arg("interval").toInt();
  host.trim();user.trim();prefix.trim();
  bool good = host.length()<=64 && user.length()<=32 &&
    pw.length()<=64 && prefix.length()>0 && prefix.length()<=64 &&
    port>0 && port<=65535 && interval>=3 && interval<=3600;
  for(size_t i=0;i<prefix.length();++i) {
    char c=prefix[i];
    if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') ||
         (c>='0'&&c<='9') || c=='/' || c=='_' || c=='-' || c=='.'))
      good=false;
  }
  if(!good) {web.send(400,"text/plain","Ungueltige MQTT-Einstellungen");return;}
  MqttSettings next=mqttSettings;
  host.toCharArray(next.host,sizeof(next.host));
  user.toCharArray(next.user,sizeof(next.user));
  prefix.toCharArray(next.prefix,sizeof(next.prefix));
  if(pw.length()) pw.toCharArray(next.password,sizeof(next.password));
  next.port=static_cast<uint16_t>(port);
  next.intervalSec=static_cast<uint16_t>(interval);
  next.mqttEnabled=web.hasArg("mqttEnabled")?1:0;
  next.optoEnabled=web.hasArg("optoEnabled")?1:0;
  memset(next.selection,0,sizeof(next.selection));
  for(uint16_t i=0;i<POINT_COUNT;++i)
    if(web.hasArg("p"+String(i))) next.selection[i/8] |= 1U<<(i%8);
  mqttSettings=next;
  if(!mqttSaveSettings()) {
    web.send(500,"text/plain","EEPROM Speicherung fehlgeschlagen");return;
  }
  MqttRuntime::stop();
  MqttRuntime::begin();
  web.sendHeader("Location","/config");
  web.send(303,"text/plain","Gespeichert");
}

void handleNetwork() {
  String h="<h1>Netzwerk-Konfiguration</h1><p><a href='/'>Startseite</a></p>";
  h+="<p>Aktuelle IP: <b>"+WiFi.localIP().toString()+"</b> / ";
  h+=String(networkSettings.staticEnabled?"Statisch":"DHCP")+"</p>";
  h+="<form method='POST' action='/network'>";
  h+="<label>IP-Konfiguration</label><select name='mode'>"
     "<option value='dhcp'";
  if(!networkSettings.staticEnabled) h+=" selected";
  h+=">DHCP (automatisch)</option><option value='static'";
  if(networkSettings.staticEnabled) h+=" selected";
  h+=">Statische IP</option></select><p>Bei statischer IP alle vier Felder ausfuellen.</p>";
  h+="<label>IP-Adresse</label><input name='ip' value='"+
      (networkSettings.staticEnabled?networkStr(networkSettings.ip):WiFi.localIP().toString())+"'>";
  h+="<label>Subnetzmaske</label><input name='mask' value='"+
      (networkSettings.staticEnabled?networkStr(networkSettings.mask):WiFi.subnetMask().toString())+"'>";
  h+="<label>Gateway</label><input name='gateway' value='"+
      (networkSettings.staticEnabled?networkStr(networkSettings.gateway):WiFi.gatewayIP().toString())+"'>";
  h+="<label>DNS-Server</label><input name='dns' value='"+
      (networkSettings.staticEnabled?networkStr(networkSettings.dns):WiFi.dnsIP().toString())+"'>";
  h+="<p><b>Achtung:</b> Nach dem Speichern startet der Wemos neu."
     " Bei statischer IP muss die neue Adresse erreichbar und im Netz frei sein.</p>";
  h+="<button type='submit'>Netzwerk speichern und neu starten</button></form>";
  web.send(200,"text/html; charset=utf-8",htmlPage(h));
}
void handleNetworkSave() {
  String mode=web.arg("mode");
  if(mode!="static" && mode!="dhcp") {
    web.send(400,"text/plain","Ungueltiger Modus");return;
  }
  NetworkSettings next=networkSettings;
  next.staticEnabled=(mode=="static")?1:0;
  if(next.staticEnabled) {
    if(!networkParse(web.arg("ip"),next.ip) ||
       !networkParse(web.arg("mask"),next.mask) ||
       !networkParse(web.arg("gateway"),next.gateway) ||
       !networkParse(web.arg("dns"),next.dns) || !networkValidate(next)) {
      web.send(400,"text/plain","Ungueltige IP, Maske, Gateway oder DNS");return;
    }
  }
  // Save separately; existing MQTT and WiFi credentials remain unchanged.
  networkSettings=next;
  if(!networkSave()) {
    web.send(500,"text/plain","Netzwerkeinstellungen nicht gespeichert");return;
  }
  web.send(200,"text/html; charset=utf-8",htmlPage(
    "<h1>Gespeichert</h1><p>Wemos startet neu. Neue Adresse: <b>"+
    String(next.staticEnabled?networkStr(next.ip):"DHCP")+"</b></p>"));
  delay(500);
  ESP.restart();
}

void handleSave() {
  if (!setupMode) { web.send(403, "text/plain", "Setup nicht aktiv"); return; }
  String ssid = web.arg("ssid"), wifi = web.arg("wifi"), ota = web.arg("ota");
  if (ssid.isEmpty() || ssid.length() > 32 || wifi.length() > 64 ||
      ota.length() < 12 || ota.length() > 64) {
    web.send(400, "text/plain", "Ungueltige Eingabe"); return;
  }
  Settings next{};
  next.magic = MAGIC;
  ssid.toCharArray(next.ssid, sizeof(next.ssid));
  wifi.toCharArray(next.wifiPassword, sizeof(next.wifiPassword));
  ota.toCharArray(next.otaPassword, sizeof(next.otaPassword));
  next.check = checksum(next);
  EEPROM.put(0, next);
  if (!EEPROM.commit()) { web.send(500, "text/plain", "Speichern fehlgeschlagen"); return; }
  web.send(200, "text/html; charset=utf-8",
           htmlPage(F("<p>Gespeichert. Wemos startet neu. Einrichtungs-WLAN verschwindet.</p>")));
  delay(500);
  ESP.restart();
}

void setup() {
  // TX/RX dienen ausschliesslich der Optolink-Kommunikation (keine USB-Debug-Ausgaben).
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  hostname = "vito-esp8266-" + String(ESP.getChipId(), HEX);
  bool valid = loadSettings();
  mqttLoadSettings();
  networkLoad();
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  if (valid) {
    WiFi.mode(WIFI_STA);
    WiFi.hostname(hostname);
    networkConfigureWiFi();
    WiFi.begin(settings.ssid, settings.wifiPassword);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis()-start < WIFI_TIMEOUT_MS) delay(100);
  }
  if (!valid || WiFi.status() != WL_CONNECTED) startSetupAP();
  web.on("/", HTTP_GET, handleRoot);
  web.on("/status", HTTP_GET, handleStatus);
  web.on("/network", HTTP_GET, handleNetwork);
  web.on("/network", HTTP_POST, handleNetworkSave);
  web.on("/config", HTTP_GET, handleConfig);
  web.on("/config", HTTP_POST, handleConfigSave);
  web.on("/save", HTTP_POST, handleSave);
  web.onNotFound([](){ web.send(404, "text/plain", "Nicht gefunden"); });
  web.begin();
  if (valid && WiFi.status() == WL_CONNECTED) startOTA();
  Optolink::begin();
  MqttRuntime::begin();
}

void loop() {
  web.handleClient();
  if (!otaInProgress) MqttRuntime::tick();
  if (otaReady) { ArduinoOTA.handle(); MDNS.update(); }
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, LOW);
    if (!otaReady && settings.magic == MAGIC) startOTA();
  } else {
    digitalWrite(LED_BUILTIN, (millis()/500) % 2);
    if (!setupMode && millis() - lastWifiRetry > WIFI_RETRY_MS) {
      lastWifiRetry = millis();
      WiFi.reconnect();
    }
  }
  delay(2);
}
