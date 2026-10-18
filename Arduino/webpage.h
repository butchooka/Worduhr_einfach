// Webinterface-Seite (eigene Datei, damit der Arduino-Präprozessor sie nicht als Code liest)
#pragma once
#include <pgmspace.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Worduhr</title>
<style>
:root{--bg:#12141a;--card:#1d212b;--fg:#e8eaf0;--mut:#9aa3b5;--acc:#4da3ff;--bd:#2c3240}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.4 system-ui,sans-serif;padding:0 0 90px}
header{padding:16px;text-align:center}
h1{margin:0;font-size:1.6rem}
#st{color:var(--mut);font-size:.9rem;margin-top:6px}
main{max-width:640px;margin:auto;padding:0 12px}
section{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:14px;margin:12px 0}
h2{margin:0 0 6px;font-size:1.05rem;color:var(--acc)}
label{display:block;margin:10px 0 4px;color:var(--mut);font-size:.85rem}
input,select,button{font:inherit;color:var(--fg);background:#10131a;border:1px solid var(--bd);border-radius:8px;padding:8px 10px;width:100%}
input[type=checkbox]{width:auto;margin-right:8px}
input[type=color]{padding:2px;height:40px}
.row{display:flex;gap:10px}.row>div{flex:1}
button{cursor:pointer;background:var(--acc);color:#04101f;border:0;font-weight:600;padding:10px}
button.sec{background:#2a3142;color:var(--fg)}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:10px}
.chk{display:flex;align-items:center;margin:10px 0}.chk label{margin:0;color:var(--fg);font-size:1rem}
small{color:var(--mut);display:block;margin-top:4px}
#toast{position:fixed;left:50%;top:14px;transform:translateX(-50%);background:#2e7d32;color:#fff;padding:8px 16px;border-radius:8px;display:none;z-index:9}
footer{position:fixed;left:0;right:0;bottom:0;background:#0b0d12;border-top:1px solid var(--bd);padding:10px 12px;text-align:center;font-size:.85rem;color:var(--mut)}
footer a{color:var(--acc);text-decoration:none;font-weight:600}
</style></head><body>
<div id="toast"></div>
<header><h1>Worduhr</h1><div id="st">lade …</div></header>
<main>

<section><h2>Farbe &amp; Helligkeit</h2>
<label>Farbe der Uhrzeit</label>
<select id="cm">
<option value="0">Warmweiß</option><option value="1">Weiß</option><option value="2">Rot</option>
<option value="3">Orange</option><option value="4">Grün</option><option value="5">Cyan</option>
<option value="6">Blau</option><option value="7">Magenta</option>
<option value="8">Zufall (neue Farbe jede Stunde)</option><option value="9">Eigene Farbe …</option>
</select>
<div id="ccw" style="display:none"><label>Eigene Farbe</label><input type="color" id="cc" value="#ffffff"></div>
<div class="row">
<div><label>Helligkeit Tag (%)</label><input type="number" id="bd" min="0" max="100"></div>
<div><label>Helligkeit Nacht (%)</label><input type="number" id="bn" min="0" max="100"></div>
</div>
<label>Strombegrenzung (mA)</label><input type="number" id="ma" min="50" max="10000" step="10">
<div class="chk"><input type="checkbox" id="md"><label for="md">Einzelminuten anzeigen (+1 … +4 MINUTEN)</label></div>
</section>

<section><h2>Tag / Nacht nach Standort</h2>
<label>Modus</label>
<select id="dn"><option value="0">Automatisch (Sonnenauf-/untergang)</option><option value="1">Immer Tag</option><option value="2">Immer Nacht</option></select>
<div class="row">
<div><label>Breitengrad</label><input type="number" id="lat" step="any"></div>
<div><label>Längengrad</label><input type="number" id="lon" step="any"></div>
</div>
<div class="row">
<div><label>Nacht-Beginn Versatz (min)</label><input type="number" id="no" min="-720" max="720"></div>
<div><label>Morgen-Tag Versatz (min)</label><input type="number" id="mo" min="-720" max="720"></div>
</div>
<small>Versatz relativ zu Sonnenuntergang bzw. Sonnenaufgang, +/− Minuten.</small>
<label>Zeitzone</label>
<select id="tzs">
<option value="CET-1CEST,M3.5.0,M10.5.0/3">Mitteleuropa (Berlin, Wien, Zürich)</option>
<option value="GMT0BST,M3.5.0/1,M10.5.0">Westeuropa (London, Lissabon)</option>
<option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Osteuropa (Athen, Helsinki)</option>
<option value="UTC0">UTC</option>
<option value="">Eigene (POSIX-String unten)</option>
</select>
<input type="text" id="tz" style="margin-top:8px">
</section>

<section><h2>Effekte</h2>
<label>Dauer (Sekunden, gilt nach „Speichern“)</label><input type="number" id="es" min="1" max="600">
<div class="grid">
<button class="sec" onclick="eff('rainbow')">Effekt Rainbow</button>
<button class="sec" onclick="eff('aurora')">Effekt Aurora</button>
<button class="sec" onclick="eff('matrix')">Effekt Matrix</button>
<button class="sec" onclick="eff('stop')">Stopp</button>
<button class="sec" onclick="eff('red')">Blinken Rot</button>
<button class="sec" onclick="eff('green')">Blinken Grün</button>
<button class="sec" onclick="eff('blue')">Blinken Blau</button>
<button class="sec" onclick="eff('white')">Blinken Weiß</button>
</div></section>

<section><h2>Uhrzeit (NTP)</h2>
<label>NTP-Server</label>
<select id="nm"><option value="0">Automatisch aus DHCP (Fallback: pool.ntp.org)</option><option value="1">Eigener Server</option></select>
<div id="nsw" style="display:none"><label>Server (Name oder IP)</label><input type="text" id="ns"></div>
</section>

<section><h2>Matrix-Ausrichtung</h2>
<div class="chk"><input type="checkbox" id="mx"><label for="mx">Links/rechts spiegeln</label></div>
<div class="chk"><input type="checkbox" id="my"><label for="my">Oben/unten spiegeln</label></div>
<div class="chk"><input type="checkbox" id="sp"><label for="sp">Zickzack-Verdrahtung (Schlange)</label></div>
<small>Steht die Schrift Kopf oder spiegelverkehrt, hier umstellen.</small>
</section>

<button onclick="save()">Einstellungen speichern</button>

<section style="margin-top:18px"><h2>WLAN</h2>
<button class="sec" onclick="scan()">Netzwerke suchen</button>
<label>Netzwerk (SSID)</label><input type="text" id="ws" list="wl" autocapitalize="none" autocomplete="off"><datalist id="wl"></datalist>
<label>Passwort</label><input type="password" id="wp" autocomplete="off">
<div class="grid">
<button onclick="wifi()">Verbinden</button>
<button class="sec" onclick="wifiReset()">WLAN vergessen</button>
</div>
<small>Ohne bekanntes WLAN öffnet die Uhr den Hotspot „Worduhr“ (ohne Passwort).</small>
</section>
</main>

<footer>
Worduhr einfach · Quellcode &amp; Doku auf <a href="https://github.com/butchooka/Worduhr_einfach" target="_blank" rel="noopener">GitHub</a>
· Mehr Modelle auf meinem <a href="https://makerworld.com/de/@butchooka" target="_blank" rel="noopener">MakerWorld-Profil</a>
</footer>

<script>
const $=i=>document.getElementById(i);
function toast(t,e){const x=$('toast');x.textContent=t;x.style.background=e?'#c62828':'#2e7d32';x.style.display='block';setTimeout(()=>x.style.display='none',2500)}
function ui(){
 $('ccw').style.display=$('cm').value==9?'block':'none';
 $('nsw').style.display=$('nm').value==1?'block':'none';
}
function tzSync(){
 const s=$('tzs');let f=false;
 for(const o of s.options){if(o.value&&o.value===$('tz').value){s.value=o.value;f=true}}
 if(!f)s.value='';
}
function fill(c){
 ['bd','bn','ma','cm','lat','lon','no','mo','dn','nm','ns','tz','es'].forEach(k=>$(k).value=c[k]);
 $('cc').value='#'+('000000'+c.cc.toString(16)).slice(-6);
 ['md','mx','my','sp'].forEach(k=>$(k).checked=!!c[k]);
 ui();tzSync();
}
function status(s){
 let t=s.tv?(s.t+' Uhr · '+(s.night?'🌙 Nacht':'☀️ Tag')+' · Aufgang '+s.sr+' · Untergang '+s.ss):'Noch keine Uhrzeit (NTP wartet)';
 let w=s.wc?('WLAN: '+s.ssid+' · '+s.ip+' · worduhr.local'):(s.ap?'Hotspot „Worduhr“ aktiv, kein WLAN verbunden':'WLAN: Verbindung wird aufgebaut …');
 if(s.wc&&s.ap)w+=' · Hotspot noch aktiv';
 $('st').innerHTML=t+'<br>'+w;
}
async function load(first){
 try{const r=await fetch('/api/state');const j=await r.json();if(first)fill(j.c);status(j.s)}catch(e){}
}
async function save(){
 const p=new URLSearchParams();
 ['bd','bn','ma','cm','lat','lon','no','mo','dn','nm','ns','tz','es'].forEach(k=>p.set(k,$(k).value));
 p.set('cc',parseInt($('cc').value.slice(1),16));
 ['md','mx','my','sp'].forEach(k=>p.set(k,$(k).checked?1:0));
 try{const r=await fetch('/api/save',{method:'POST',body:p});toast(r.ok?'Gespeichert':'Fehler beim Speichern',!r.ok)}catch(e){toast('Keine Verbindung',1)}
 load(false);
}
function eff(n){fetch('/api/effect',{method:'POST',body:new URLSearchParams({n})}).catch(()=>{})}
async function scan(){
 toast('Suche läuft …');
 try{const r=await fetch('/api/scan');const l=await r.json();
 $('wl').innerHTML=l.map(s=>'<option value="'+s.replace(/"/g,'&quot;')+'">').join('');
 toast(l.length+' Netzwerke gefunden');}catch(e){toast('Suche fehlgeschlagen',1)}
}
async function wifi(){
 if(!$('ws').value){toast('SSID fehlt',1);return}
 await fetch('/api/wifi',{method:'POST',body:new URLSearchParams({s:$('ws').value,p:$('wp').value})});
 toast('Verbinde … Status oben beobachten');
}
async function wifiReset(){
 if(!confirm('Gespeichertes WLAN vergessen? Die Uhr öffnet dann den Hotspot.'))return;
 await fetch('/api/wifireset',{method:'POST'});toast('WLAN gelöscht');
}
$('cm').onchange=ui;$('nm').onchange=ui;
$('tzs').onchange=()=>{if($('tzs').value)$('tz').value=$('tzs').value};
$('tz').oninput=tzSync;
load(true);setInterval(()=>load(false),4000);
</script></body></html>)rawliteral";
