# VitoESP8266 – Vitotronic 200 KW2

**Firmware:** v0.5.3, Wemos D1 mini (ESP8266).
**Wemos:** http://192.168.3.138/ (DHCP; Adresse kann sich ändern)
**MQTT:** 192.168.3.100:1883, Prefix vito/optolink

## System

Der Wemos liest die Vitotronic KW2 über Optolink und veröffentlicht
die ausgewählten Datenpunkte per MQTT. Node-RED (Flow vitocontrol)
speichert Messwerte in der MySQL-Tabelle db_node_red.vito_readings.
FHEM läuft vorübergehend parallel, vcontrold wurde gestoppt und deaktiviert.

## Firmware bauen und OTA installieren

pio run -e d1_mini_ota
pio run -e d1_mini_ota -t upload

Lokale Konfiguration: platformio.example.ini nach platformio.ini kopieren und
OTA-Passwort lokal eintragen. src/local_secrets.example.h nach
src/local_secrets.h kopieren und WLAN-Setup-Passwort lokal eintragen.
Beide echten Dateien sind durch .gitignore ausgeschlossen.

## Produktive Quellen in src/

- main.cpp: Web-Übersicht, Einstellungen, Netzwerk und OTA.
- optolink.h: Optolink über serielle Schnittstelle.
- mqtt_catalog.h: Datenpunktdefinitionen.
- mqtt_settings.h: MQTT und Datenpunktauswahl im EEPROM.
- mqtt_runtime.h: Polling, MQTT und geprüfte Betriebsartenbefehle.
- network_settings.h: DHCP oder statische IP im EEPROM.

## MQTT

- vito/optolink/getBrennerStunden1: Laufzeitzähler, 4-Byte Little Endian Sekunden.
- vito/optolink/getOeldurchsatz: Codierung 26, 45 entspricht 4,5 l/h.
- vito/optolink/getOelverbrauch: interner Ölverbrauch (Skalierung prüfen).
- vito/optolink/getBetriebArtM1: aktuelle Betriebsart.
- vito/optolink/setMode: HWW, WW, OFF (niemals retained).
- vito/optolink/setMode/result: Ergebnis nach Rücklesen.
- vito/optolink/status: online/offline.
- vito/BurnerSeconds1: dekodierter Laufzeitzähler aus Node-RED.

Seit v0.5.3 werden Brennerstunden, Öldurchsatz und Ölverbrauch in der
Web-Übersicht mit Einheiten angezeigt. MQTT-Rohformate unverändert.

## MiniHome-Server

- /home/dougie/getheizung.sh: liest MQTT statt vclient,
  veröffentlicht bisherige FHEM- und MQTT-Werte weiter.
  /etc/crontab ruft es alle fünf Minuten auf.
- /home/dougie/vc_setMode.sh: MQTT-basierte Betriebsartensteuerung,
  mit Beachtung des externen 1-Wire-Heizkreispumpenrelais.
- Alte vc_setHWW.sh, vc_setWW.sh, vc_setOFF.sh benötigen vclient.
- vcontrold wurde am 9. November 2026 gestoppt und deaktiviert.

## Referenzen und Sicherungen

reference/: wichtige historische KW2-Kataloge und Scan-Auswertung.
backup/: letzter Quellcode-Stand der v0.5.2 als Rückfalloption.

Die ausführliche Dokumentation ist separat in
VitoESP8266_KW2_Migration_2026-11-09.md verfügbar.

## Heizungsueberwachung

Der Node-RED-Tab Vito Monitoring prueft den KW2-Sammelstatus
und die Optolink-Kommunikation. Details in integration/node-red/README.md.
