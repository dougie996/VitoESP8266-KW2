// Heating supervision: Viessmann KW2 general fault + Wemos/Optolink
// Does NOT control the heating system. It only sends alert email and LED16 lamp 9.
// Inputs: vito/optolink/# (including status and getStatusStoerung), 60-s heartbeat.
const now=Date.now();
const WEMOS_OFFLINE_MS=120000;    // debounce explicit MQTT LWT
const DATA_TIMEOUT_MS=8*60000;   // enough for 14 x 10 s polling + retries
let s=context.get("state");
if (!s || !Number.isFinite(s.started)) {
 s={started:now,status:null,statusSince:now,lastValid:0,lastFault:0,
    faultRaw:"00",active:{},led:null};
}
const topic=String(msg.topic||"");
if (topic==="vito/optolink/status") {
 let value=String(msg.payload||"").trim().toLowerCase();
 if (value==="online"||value==="offline") {
  if (s.status!==value) {s.status=value;s.statusSince=now;}
 }
} else if (topic.startsWith("vito/optolink/get")) {
 let p=msg.payload;
 if (Buffer.isBuffer(p)) p=p.toString("utf8");
 if (typeof p==="string") {try{p=JSON.parse(p);}catch(e){p=null;}}
 if (p && p.valid===true && typeof p.raw==="string" &&
     /^(?:[0-9a-fA-F]{2})(?: [0-9a-fA-F]{2})*$/.test(p.raw.trim())) {
  // A successful read is actual evidence that Optolink returns data.
  s.lastValid=now;
  if (topic==="vito/optolink/getStatusStoerung" &&
      /^[0-9a-fA-F]{2}$/.test(p.raw.trim())) {
   s.lastFault=now;
   s.faultRaw=p.raw.trim().toUpperCase();
  }
 }
}
const current={};
if (s.faultRaw!=="00" && s.lastFault>0)
 current.KW2_FAULT="Vitotronic meldet Sammelstoerung, Rohwert 0x"+s.faultRaw+
  ". Genaue Ursache an der Regelung/Fehlerhistorie pruefen.";
if (s.status==="offline" && now-s.statusSince>=WEMOS_OFFLINE_MS)
 current.WEMOS_OFFLINE="Wemos meldet MQTT-Status offline (seit mindestens 2 Minuten).";
if (now-s.started>=DATA_TIMEOUT_MS &&
    (!s.lastValid || now-s.lastValid>=DATA_TIMEOUT_MS))
 current.OPTOLINK_NO_DATA="Keine frischen gueltigen Optolink-Lesewerte seit mindestens 8 Minuten.";
if (now-s.started>=DATA_TIMEOUT_MS &&
    s.status!=="offline" && !current.OPTOLINK_NO_DATA &&
    (!s.lastFault || now-s.lastFault>=DATA_TIMEOUT_MS))
 current.FAULT_STATUS_MISSING="Sammelstoerungswert getStatusStoerung seit mindestens 8 Minuten nicht gueltig empfangen.";
const prev=s.active||{},mails=[];
const names={KW2_FAULT:"Vitotronic Sammelstoerung",WEMOS_OFFLINE:"Wemos nicht erreichbar",
 OPTOLINK_NO_DATA:"Optolink liefert keine Daten",FAULT_STATUS_MISSING:"Fehlerstatus nicht lesbar"};
function email(id,kind,detail) {
 return {topic:"[H3 Heizung] "+kind+": "+names[id],
   payload:"Heizungsueberwachung MiniHome-Server\n"+
    "Zeit: "+new Date(now).toISOString()+" (UTC)\n"+
    "Ereignis: "+kind+"\nKategorie: "+names[id]+"\n"+
    "Details: "+detail+"\n"+
    "Wemos MQTT-Status: "+(s.status||"noch nicht empfangen")+"\n"+
    "Letzte gueltige Optolink-Lesung: "+(s.lastValid?new Date(s.lastValid).toISOString():"keine")+"\n"+
    "Letzter Sammelstoerungswert: 0x"+s.faultRaw+"\n"+
    "Wemos: http://192.168.3.138/status\n"+
    "Node-RED: http://192.168.3.100:1880/dashboard/\n"+
    "Hinweis: Sammelstoerung ist kein decodierter Viessmann-Fehlercode."};
}
for (const id of Object.keys(current))
 if (!Object.prototype.hasOwnProperty.call(prev,id))
  mails.push(email(id,"ALARM",current[id]));
for (const id of Object.keys(prev))
 if (!Object.prototype.hasOwnProperty.call(current,id))
  mails.push(email(id,"ENTSTOERT",prev[id]));
const activeKeys=Object.keys(current);
const desiredLed=activeKeys.length?"Rot":"Aus";
const ledMsg=s.led===desiredLed?null:
 {topic:"h3/hmouled16/e030bf/led/9/set",payload:desiredLed,retain:false};
s.led=desiredLed;
s.active=current;
context.set("state",s);
const health=activeKeys.length?"ALARM":"OK";
node.status({fill:activeKeys.length?"red":"green",shape:"dot",
 text:health+" | Optolink "+(s.lastValid?Math.round((now-s.lastValid)/1000)+"s":"keine Daten")+
 " | KW2 0x"+s.faultRaw});
const status={state:health,active:current,wemos:s.status,
 fault_raw:s.faultRaw,last_valid:s.lastValid?new Date(s.lastValid).toISOString():null,
 last_fault:s.lastFault?new Date(s.lastFault).toISOString():null,
 checked_at:new Date(now).toISOString()};
return [
 mails.length?mails:null,
 ledMsg,
 {topic:"h3/heating/monitor/status",payload:JSON.stringify(status),retain:true},
 {payload:health+(activeKeys.length?" – "+activeKeys.map(x=>names[x]).join("; "):
 " – Wemos "+(s.status||"unbekannt")+", KW2 0x"+s.faultRaw)}
];
