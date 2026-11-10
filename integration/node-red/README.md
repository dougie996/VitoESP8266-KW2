# Node-RED Heizungsueberwachung, 10. November 2026

Produktiver Flow: Vito Monitoring auf MiniHome-Server
(http://192.168.3.100:1880/).
Die Datei heating_monitor_v1.js ist eine Kopie der produktiven
Node-RED-Funktion. Bei Aenderungen am Flow diese Kopie synchronisieren.

## Datenquellen

- vito/optolink/status: Wemos MQTT-LWT online oder offline.
- vito/optolink/getStatusStoerung: KW2 Sammelstoerung, Optolink 0x7579;
  ein Byte, 00 bedeutet aktuell keine Sammelstoerung.
- vito/optolink/get...: andere gueltige Optolink-Lesewerte als Lebenszeichen.

## Alarmregeln

- Sammelstoerungs-Rohwert ungleich 00 und gueltig: Alarm.
- Wemos LWT offline ueber 2 Minuten: Alarm.
- Acht Minuten ohne gueltige Optolink-Werte: Alarm.
- Acht Minuten ohne frischen Sammelstoerungswert bei sonst
  funktionierenden Optolink-Daten: Alarm.
- Nach Node-RED-Neustart acht Minuten Anlaufzeit fuer fehlende Daten.
- Pro Stoerung nur eine Mail beim Auftreten und eine bei Entstoerung.
- HM-LED16 Lampe 9: Rot bei irgendeinem Alarm, sonst Aus.

## Anbindungen

- Vorhandener Send Email Node 
- Vorhandene H3 MQTT Strecke 
- LED9 MQTT: h3/hmouled16/e030bf/led/9/set.
- Status MQTT retained: h3/heating/monitor/status.
- Anzeige im Dashboard-2, Gruppe H3.

Die Alarmlogik ist lokal getestet. Der physische Buzzer und der
Mailversand wurden nicht durch kuenstliche Stoerungen ausgeloest.

## Diagnose und Grenzen

Die Adresse 0x7579 liefert einen Sammelstatus, nicht den
Viessmann-Klartextfehlercode. Details an der Vitotronic bzw.
in der Fehlerhistorie pruefen. Unterscheidung Wemos-Offline,
fehlende Optolink-Daten und Heizungsstoerung ist wesentlich.

Vollstaendige Node-RED-Sicherung vor dem Deployment:
backup/nodered_pre_heating_monitor_20261110.json
(lokal, bewusst vom GitHub-Upload ausgeschlossen).
