#include "web.h"
#include "../storage.h"
#include "../mqtt.h"
#include "../motor.h"
#include "../door.h"
#include "../logger.h"
#include <ArduinoJson.h>

// ==================================================
// EINSTELLUNGEN
// ==================================================
void handleSettings()
{
    // Head sofort senden → Browser bekommt CSS und fängt an zu rendern
    sendHTMLStart(200);
    sendHTMLChunk(renderThemeHead("Einstellungen"));

    // Body mit Platzhaltern aufbauen (nur ~6KB statt 9KB replace)
    String body = R"rawliteral(
<div class="header"><h3>⚙️ Einstellungen</h3></div>
<div class="container">
  <div class="card">
    <div class="card-title">Betriebsmodus</div>
    <div class="tabs">
      <button class="tab-btn active" onclick="showTab('open')">Öffnen</button>
      <button class="tab-btn"        onclick="showTab('close')">Schließen</button>
    </div>
    <div class="switch-row">
      <span>Endschalter verwenden</span>
      <label class="switch">
        <input type="checkbox" id="limitSwitches" %LIMIT_SWITCHES%>
        <span class="slider"></span>
      </label>
    </div><br>
  </div>

  <div class="card" id="openCard">
    <div class="card-title">Öffnungs-Einstellungen</div>
    <form id="openTab">
      <label>Modus</label>
      <select name="openMode" id="openMode" onchange="toggleOpen()">
        <option value="time">Zeit</option>
        <option value="light">Helligkeit</option>
      </select>
      <div id="openTimeField"><label>Öffnungszeit</label><input type="time" name="openTime"></div>
      <div id="openLightField" style="display:none;"><label>Licht-Schwelle (Lux)</label><input type="number" name="openLightThreshold"></div>
      <label>Licht vorher (Min)</label><input type="number" name="lampPreOpen">
      <label>Licht nachher (Min)</label><input type="number" name="lampPostOpen">
      <button type="submit" class="btn-open">Speichern</button>
    </form>
  </div>

  <div class="card" id="closeCard" style="display:none;">
    <div class="card-title">Schließ-Einstellungen</div>
    <form id="closeTab">
      <label>Modus</label>
      <select name="closeMode" id="closeMode" onchange="toggleClose()">
        <option value="time">Zeit</option>
        <option value="light">Helligkeit</option>
      </select>
      <div id="closeTimeField"><label>Schließzeit</label><input type="time" name="closeTime"></div>
      <div id="closeLightField" style="display:none;"><label>Licht-Schwelle (Lux)</label><input type="number" name="closeLightThreshold"></div>
      <div id="closeLightDelayField" style="display:none;"><label>Verzögerung nach Lux-Schwelle (Min)</label><input type="number" name="closeDelayMin" min="0" max="30" placeholder="0"><div style="font-size:11px;color:var(--muted);margin-top:2px;">Klappe bleibt nach Erreichen des Schwellwerts noch X Minuten offen</div></div>
      <label>Licht vorher (Min)</label><input type="number" name="lampPreClose">
      <label>Licht nachher (Min)</label><input type="number" name="lampPostClose">
      <button type="submit" class="btn-close">Speichern</button>
    </form>
  </div>

  <div id="saveMsg" class="card" style="display:none;text-align:center;font-weight:600;"></div>
</div>

<style>
.tabs{display:flex;gap:10px;margin-top:10px;}
.tab-btn{flex:1;padding:8px;border-radius:12px;border:none;font-weight:600;font-size:14px;background:var(--bg);color:var(--text);}
.tab-btn.active{background:var(--green);color:white;}
label{display:block;font-weight:600;margin-top:14px;margin-bottom:6px;font-size:14px;color:var(--muted);}
input,select{width:100%;padding:8px 10px;border-radius:10px;border:1px solid #e5e7eb;font-size:14px;background:var(--card);color:var(--text);height:38px;box-sizing:border-box;}
input[type=time]{width:auto;min-width:120px;max-width:160px;}
.switch-row{display:flex;justify-content:space-between;align-items:center;margin:14px 0;}
.switch{position:relative;display:inline-block;width:52px;height:28px;}
.switch input{opacity:0;width:0;height:0;}
.slider{position:absolute;cursor:pointer;inset:0;background-color:#2c3e50;border-radius:34px;transition:0.25s;}
.slider:before{position:absolute;content:"";height:22px;width:22px;left:3px;bottom:3px;background-color:white;border-radius:50%;transition:0.25s;box-shadow:0 2px 6px rgba(0,0,0,0.4);}
input:checked+.slider{background-color:#27ae60;}
input:checked+.slider:before{transform:translateX(24px);}
</style>
<script>
function showTab(w){
  document.getElementById('openCard').style.display  = w==='open'?'block':'none';
  document.getElementById('closeCard').style.display = w==='close'?'block':'none';
  const tabs=document.querySelectorAll('.tab-btn');
  tabs[0].classList.toggle('active',w==='open'); tabs[1].classList.toggle('active',w==='close');
}
function toggleOpen(){ const m=document.getElementById('openMode').value; document.getElementById('openTimeField').style.display=m==='time'?'block':'none'; document.getElementById('openLightField').style.display=m==='light'?'block':'none'; }
function toggleClose(){ const m=document.getElementById('closeMode').value; document.getElementById('closeTimeField').style.display=m==='time'?'block':'none'; document.getElementById('closeLightField').style.display=m==='light'?'block':'none'; document.getElementById('closeLightDelayField').style.display=m==='light'?'block':'none'; }
document.getElementById('openTab').onsubmit=e=>{e.preventDefault();fetch('/save-open',{method:'POST',body:new FormData(e.target)}).then(()=>showMsg("Öffnen gespeichert",true));};
document.getElementById('closeTab').onsubmit=e=>{e.preventDefault();fetch('/save-close',{method:'POST',body:new FormData(e.target)}).then(()=>showMsg("Schließen gespeichert",true));};
function showMsg(text,ok){const m=document.getElementById("saveMsg");m.innerText=text;m.style.background=ok?"rgba(34,197,94,0.15)":"rgba(239,68,68,0.15)";m.style.color=ok?"var(--green)":"var(--red)";m.style.display="block";setTimeout(()=>m.style.display="none",1500);}
document.getElementById('openMode').value="%OPEN_MODE%";
document.querySelector('[name="openTime"]').value="%OPEN_TIME%";
document.querySelector('[name="openLightThreshold"]').value="%OPEN_LIGHT%";
document.querySelector('[name="lampPreOpen"]').value="%LAMP_PRE_OPEN%";
document.querySelector('[name="lampPostOpen"]').value="%LAMP_POST_OPEN%";
document.getElementById('closeMode').value="%CLOSE_MODE%";
document.querySelector('[name="closeTime"]').value="%CLOSE_TIME%";
document.querySelector('[name="closeLightThreshold"]').value="%CLOSE_LIGHT%";
document.querySelector('[name="lampPreClose"]').value="%LAMP_PRE_CLOSE%";
document.querySelector('[name="lampPostClose"]').value="%LAMP_POST_CLOSE%";
document.querySelector('[name="closeDelayMin"]').value="%CLOSE_DELAY_MIN%";
showTab('open'); toggleOpen(); toggleClose();
document.getElementById("limitSwitches").addEventListener("change",function(){
  fetch("/set-limit-switches",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"enabled="+(this.checked?1:0)}).then(()=>showMsg("Endschalter gespeichert",true));
});
</script>
)rawliteral";

    // Replacements nur auf Body-String (~6KB statt 9KB)
    body.replace("%OPEN_MODE%",       openMode);
    body.replace("%OPEN_TIME%",       openTime);
    body.replace("%OPEN_LIGHT%",      String(openLightThreshold));
    body.replace("%LAMP_PRE_OPEN%",   String(lampPreOpen));
    body.replace("%LAMP_POST_OPEN%",  String(lampPostOpen));
    body.replace("%CLOSE_MODE%",      closeMode);
    body.replace("%CLOSE_TIME%",      closeTime);
    body.replace("%CLOSE_LIGHT%",     String(closeLightThreshold));
    body.replace("%LAMP_PRE_CLOSE%",  String(lampPreClose));
    body.replace("%LAMP_POST_CLOSE%", String(lampPostClose));
    body.replace("%CLOSE_DELAY_MIN%",  String(closeDelayMin));
    body.replace("%LIMIT_SWITCHES%",  useLimitSwitches ? "checked" : "");
    sendHTMLChunk(body);

    sendHTMLChunk(renderFooter());
    sendHTMLEnd();
}

// ==================================================
// SAVE OPEN
// ==================================================
void handleSaveOpen()
{
    String err;
    JsonDocument doc;
    doc["openMode"]           = server.arg("openMode");
    doc["openTime"]           = server.arg("openTime");
    doc["openLightThreshold"] = server.arg("openLightThreshold").toInt();
    doc["lampPreOpen"]        = server.arg("lampPreOpen").toInt();
    doc["lampPostOpen"]       = server.arg("lampPostOpen").toInt();
    // keep existing close values
    doc["closeMode"]            = closeMode;
    doc["closeTime"]            = closeTime;
    doc["closeLightThreshold"]  = closeLightThreshold;
    doc["lampPreClose"]         = lampPreClose;
    doc["lampPostClose"]        = lampPostClose;

    if (applySettingsFromJson(doc, err))
    {
        addLog("Öffnungs-Einstellungen gespeichert");
        sendPlain("OK");
    }
    else sendPlain(err, 400);
}

// ==================================================
// SAVE CLOSE
// ==================================================
void handleSaveClose()
{
    String err;
    JsonDocument doc;
    // keep existing open values
    doc["openMode"]             = openMode;
    doc["openTime"]             = openTime;
    doc["openLightThreshold"]   = openLightThreshold;
    doc["lampPreOpen"]          = lampPreOpen;
    doc["lampPostOpen"]         = lampPostOpen;
    doc["closeMode"]            = server.arg("closeMode");
    doc["closeTime"]            = server.arg("closeTime");
    doc["closeLightThreshold"]  = server.arg("closeLightThreshold").toInt();
    doc["lampPreClose"]         = server.arg("lampPreClose").toInt();
    doc["lampPostClose"]        = server.arg("lampPostClose").toInt();
    closeDelayMin = constrain(server.arg("closeDelayMin").toInt(), 0, 30);
    saveCloseDelay();

    if (applySettingsFromJson(doc, err))
    {
        addLog("Schließ-Einstellungen gespeichert");
        sendPlain("OK");
    }
    else sendPlain(err, 400);
}

// ==================================================
// EINSTELLUNGEN KLAPPE 2
// ==================================================
void handleDoor2Settings()
{
    sendHTMLStart(200);
    sendHTMLChunk(renderThemeHead("Klappe 2 Einstellungen"));

    String body = R"rawliteral(
<div class="header"><h3>⚙️ Klappe 2 Einstellungen</h3></div>
<div class="container">
  <div class="card">
    <div class="card-title">Betriebsmodus Klappe 2</div>
    <div class="tabs">
      <button class="tab-btn active" onclick="showTab2('open')">Öffnen</button>
      <button class="tab-btn"        onclick="showTab2('close')">Schließen</button>
    </div>
    <div class="switch-row">
      <span>Endschalter verwenden</span>
      <label class="switch">
        <input type="checkbox" id="limitSwitches2" %LIMIT_SWITCHES2%>
        <span class="slider"></span>
      </label>
    </div><br>
  </div>

  <div class="card" id="openCard2">
    <div class="card-title">Öffnungs-Einstellungen</div>
    <form id="openTab2">
      <label>Modus</label>
      <select name="openMode" id="openMode2" onchange="toggleOpen2()">
        <option value="time">Zeit</option>
        <option value="light">Helligkeit</option>
      </select>
      <div id="openTimeField2"><label>Öffnungszeit</label><input type="time" name="openTime"></div>
      <div id="openLightField2" style="display:none;"><label>Licht-Schwelle (Lux)</label><input type="number" name="openLightThreshold"></div>
      <button type="submit" class="btn-open">Speichern</button>
    </form>
  </div>

  <div class="card" id="closeCard2" style="display:none;">
    <div class="card-title">Schließ-Einstellungen</div>
    <form id="closeTab2">
      <label>Modus</label>
      <select name="closeMode" id="closeMode2" onchange="toggleClose2()">
        <option value="time">Zeit</option>
        <option value="light">Helligkeit</option>
      </select>
      <div id="closeTimeField2"><label>Schließzeit</label><input type="time" name="closeTime"></div>
      <div id="closeLightField2" style="display:none;"><label>Licht-Schwelle (Lux)</label><input type="number" name="closeLightThreshold"></div>
      <div id="closeLightDelayField2" style="display:none;"><label>Verzögerung nach Lux-Schwelle (Min)</label><input type="number" name="closeDelayMin" min="0" max="30" placeholder="0"><div style="font-size:11px;color:var(--muted);margin-top:2px;">Klappe bleibt nach Erreichen des Schwellwerts noch X Minuten offen</div></div>
      <button type="submit" class="btn-close">Speichern</button>
    </form>
  </div>

  <div class="card">
    <div class="card-title">🎯 Kalibrierung Klappe 2</div>
    <p style="font-size:13px;color:var(--muted);margin:0 0 12px;">Messe die Laufzeit des Motors in jede Richtung. Klicke Start – der Motor fährt los. Klicke Stopp wenn die Endlage erreicht ist.</p>
    <div style="display:flex;gap:10px;margin-bottom:14px;">
      <div style="flex:1;background:var(--bg);padding:14px;border-radius:12px;text-align:center;">
        <div style="font-size:12px;color:var(--muted);">Öffnen</div>
        <div id="cal2Open" style="font-weight:bold;font-size:20px;">%D2_OPEN_POS%</div>
        <div style="font-size:11px;color:var(--muted);">ms</div>
      </div>
      <div style="flex:1;background:var(--bg);padding:14px;border-radius:12px;text-align:center;">
        <div style="font-size:12px;color:var(--muted);">Schließen</div>
        <div id="cal2Close" style="font-weight:bold;font-size:20px;">%D2_CLOSE_POS%</div>
        <div style="font-size:11px;color:var(--muted);">ms</div>
      </div>
    </div>

    <div class="card-title" style="font-size:14px;margin-top:16px;">Stoppuhr-Messung</div>
    <div id="calStatus" style="font-size:14px;color:var(--muted);margin-bottom:10px;">Bereit</div>
    <div id="calTimer" style="font-size:28px;font-weight:700;text-align:center;margin:8px 0;">0.0 s</div>
    <div style="display:flex;gap:8px;">
      <button id="calOpenBtn" onclick="calStart('open')" style="flex:1;background:var(--green);color:white;">▲ Öffnen messen</button>
      <button id="calCloseBtn" onclick="calStart('close')" style="flex:1;background:var(--red);color:white;">▼ Schließen messen</button>
    </div>
    <button id="calStopBtn" onclick="calStop()" style="display:none;background:var(--orange);color:white;margin-top:8px;">⏹ Stopp &amp; Speichern</button>

    <div class="card-title" style="font-size:14px;margin-top:20px;">Manuelle Steuerung</div>
    <div style="display:flex;gap:8px;">
      <button onclick="motor2('up')"   style="flex:1;background:var(--green);color:white;">▲ Auf</button>
      <button onclick="motor2('stop')" style="flex:1;background:var(--orange);color:white;">⏹ Stop</button>
      <button onclick="motor2('down')" style="flex:1;background:var(--red);color:white;">▼ Zu</button>
    </div>

    <div class="card-title" style="font-size:14px;margin-top:20px;">Manuell eingeben</div>
    <div style="display:flex;gap:8px;align-items:flex-end;">
      <div style="flex:1;"><label style="font-size:12px;">Öffnen (ms)</label><input type="number" id="manOpen" value="%D2_OPEN_POS%" min="500" max="60000"></div>
      <div style="flex:1;"><label style="font-size:12px;">Schließen (ms)</label><input type="number" id="manClose" value="%D2_CLOSE_POS%" min="500" max="60000"></div>
      <button onclick="manSave()" style="width:80px;height:38px;background:var(--green);color:white;font-size:13px;">💾</button>
    </div>
  </div>

  <div class="card">
    <div class="card-title">⚡ Blockade-Schwelle Klappe 2</div>
    <p style="font-size:13px;color:var(--muted);margin:0 0 10px;">Eigene Schwelle für den Motor der zweiten Klappe. Die Erkennung (aktiv/inaktiv) gilt global für beide Klappen.</p>
    <div style="display:flex;gap:8px;align-items:flex-end;">
      <div style="flex:1;"><label style="font-size:12px;">Schwelle (A)</label><input type="number" id="d2Threshold" value="%D2_BLOCKADE%" min="0.5" max="10" step="0.1"></div>
      <button onclick="saveD2Blockade()" style="width:120px;height:38px;background:var(--green);color:white;font-size:13px;">💾 Speichern</button>
    </div>
    <div style="font-size:12px;color:var(--muted);margin-top:6px;">Klappe 1 Schwelle: %D1_BLOCKADE% A</div>
  </div>

  <div id="saveMsg2" class="card" style="display:none;text-align:center;font-weight:600;"></div>
</div>

<style>
.tabs{display:flex;gap:10px;margin-top:10px;}
.tab-btn{flex:1;padding:8px;border-radius:12px;border:none;font-weight:600;font-size:14px;background:var(--bg);color:var(--text);}
.tab-btn.active{background:var(--green);color:white;}
label{display:block;font-weight:600;margin-top:14px;margin-bottom:6px;font-size:14px;color:var(--muted);}
input,select{width:100%;padding:8px 10px;border-radius:10px;border:1px solid #e5e7eb;font-size:14px;background:var(--card);color:var(--text);height:38px;box-sizing:border-box;}
input[type=time]{width:auto;min-width:120px;max-width:160px;}
.switch-row{display:flex;justify-content:space-between;align-items:center;margin:14px 0;}
.switch{position:relative;display:inline-block;width:52px;height:28px;}
.switch input{opacity:0;width:0;height:0;}
.slider{position:absolute;cursor:pointer;inset:0;background-color:#2c3e50;border-radius:34px;transition:0.25s;}
.slider:before{position:absolute;content:"";height:22px;width:22px;left:3px;bottom:3px;background-color:white;border-radius:50%;transition:0.25s;box-shadow:0 2px 6px rgba(0,0,0,0.4);}
input:checked+.slider{background-color:#27ae60;}
input:checked+.slider:before{transform:translateX(24px);}
.status-row{display:flex;justify-content:space-between;margin-bottom:10px;font-size:15px;}
.label{color:var(--muted);}
</style>
<script>
function showTab2(w){
  document.getElementById('openCard2').style.display  = w==='open'?'block':'none';
  document.getElementById('closeCard2').style.display = w==='close'?'block':'none';
  const tabs=document.querySelectorAll('.tab-btn');
  tabs[0].classList.toggle('active',w==='open'); tabs[1].classList.toggle('active',w==='close');
}
function toggleOpen2(){ const m=document.getElementById('openMode2').value; document.getElementById('openTimeField2').style.display=m==='time'?'block':'none'; document.getElementById('openLightField2').style.display=m==='light'?'block':'none'; }
function toggleClose2(){ const m=document.getElementById('closeMode2').value; document.getElementById('closeTimeField2').style.display=m==='time'?'block':'none'; document.getElementById('closeLightField2').style.display=m==='light'?'block':'none'; document.getElementById('closeLightDelayField2').style.display=m==='light'?'block':'none'; }
document.getElementById('openTab2').onsubmit=e=>{e.preventDefault();fetch('/save-door2-open',{method:'POST',body:new FormData(e.target)}).then(()=>showMsg2("Öffnen gespeichert",true));};
document.getElementById('closeTab2').onsubmit=e=>{e.preventDefault();fetch('/save-door2-close',{method:'POST',body:new FormData(e.target)}).then(()=>showMsg2("Schließen gespeichert",true));};
function showMsg2(text,ok){const m=document.getElementById("saveMsg2");m.innerText=text;m.style.background=ok?"rgba(34,197,94,0.15)":"rgba(239,68,68,0.15)";m.style.color=ok?"var(--green)":"var(--red)";m.style.display="block";setTimeout(()=>m.style.display="none",1500);}
function motor2(cmd){fetch('/motor2/'+cmd,{cache:'no-store'});}

// Stoppuhr-Kalibrierung
var calDir=null, calT0=0, calInterval=null;
function calStart(dir){
  if(calInterval) calStop();
  calDir=dir;
  calT0=Date.now();
  document.getElementById('calStatus').innerText=dir==='open'?'⬆ Motor fährt auf...':'⬇ Motor fährt zu...';
  document.getElementById('calStatus').style.color=dir==='open'?'var(--green)':'var(--red)';
  document.getElementById('calStopBtn').style.display='block';
  document.getElementById('calOpenBtn').style.display='none';
  document.getElementById('calCloseBtn').style.display='none';
  fetch('/motor2/'+(dir==='open'?'up':'down'),{cache:'no-store'});
  calInterval=setInterval(function(){
    var ms=Date.now()-calT0;
    document.getElementById('calTimer').innerText=(ms/1000).toFixed(1)+' s';
  },100);
}
function calStop(){
  if(!calInterval) return;
  clearInterval(calInterval); calInterval=null;
  var ms=Date.now()-calT0;
  fetch('/motor2/stop',{cache:'no-store'});
  document.getElementById('calStopBtn').style.display='none';
  document.getElementById('calOpenBtn').style.display='inline-block';
  document.getElementById('calCloseBtn').style.display='inline-block';
  // Speichern
  var body='dir='+calDir+'&ms='+ms;
  fetch('/save-door2-calib',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})
    .then(function(){
      if(calDir==='open') document.getElementById('cal2Open').innerText=ms;
      else                document.getElementById('cal2Close').innerText=ms;
      document.getElementById('calStatus').innerText='Gespeichert: '+ms+' ms';
      document.getElementById('calStatus').style.color='var(--green)';
      showMsg2((calDir==='open'?'Öffnungszeit':'Schließzeit')+' gespeichert: '+ms+' ms',true);
    });
  calDir=null;
}
function manSave(){
  var o=document.getElementById('manOpen').value;
  var c=document.getElementById('manClose').value;
  fetch('/save-door2-calib-manual',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'open='+o+'&close='+c})
    .then(function(){
      document.getElementById('cal2Open').innerText=o;
      document.getElementById('cal2Close').innerText=c;
      showMsg2('Positionen gespeichert',true);
    });
}
function saveD2Blockade(){
  var v=document.getElementById('d2Threshold').value;
  fetch('/save-door2-blockade',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'threshold='+v})
    .then(function(){showMsg2('Blockade-Schwelle gespeichert: '+v+' A',true);});
}
document.getElementById('openMode2').value="%D2_OPEN_MODE%";
document.querySelector('#openTab2 [name="openTime"]').value="%D2_OPEN_TIME%";
document.querySelector('#openTab2 [name="openLightThreshold"]').value="%D2_OPEN_LIGHT%";
document.getElementById('closeMode2').value="%D2_CLOSE_MODE%";
document.querySelector('#closeTab2 [name="closeTime"]').value="%D2_CLOSE_TIME%";
document.querySelector('#closeTab2 [name="closeLightThreshold"]').value="%D2_CLOSE_LIGHT%";
document.querySelector('#closeTab2 [name="closeDelayMin"]').value="%D2_CLOSE_DELAY%";
showTab2('open'); toggleOpen2(); toggleClose2();
document.getElementById("limitSwitches2").addEventListener("change",function(){
  fetch("/set-door2-limit-switches",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"enabled="+(this.checked?1:0)}).then(()=>showMsg2("Endschalter gespeichert",true));
});
</script>
)rawliteral";

    body.replace("%D2_OPEN_MODE%",    door2OpenMode);
    body.replace("%D2_OPEN_TIME%",    door2OpenTime);
    body.replace("%D2_OPEN_LIGHT%",   String(door2OpenLightThreshold));
    body.replace("%D2_CLOSE_MODE%",   door2CloseMode);
    body.replace("%D2_CLOSE_TIME%",   door2CloseTime);
    body.replace("%D2_CLOSE_LIGHT%",  String(door2CloseLightThreshold));
    body.replace("%D2_CLOSE_DELAY%",  String(door2CloseDelayMin));
    body.replace("%D2_OPEN_POS%",     String(door2OpenPosition));
    body.replace("%D2_CLOSE_POS%",    String(door2ClosePosition));
    body.replace("%LIMIT_SWITCHES2%", door2UseLimitSwitches ? "checked" : "");
    body.replace("%D2_BLOCKADE%",    String(door2BlockadeThresholdA, 1));
    body.replace("%D1_BLOCKADE%",    String(blockadeThresholdA, 1));
    sendHTMLChunk(body);

    sendHTMLChunk(renderFooter());
    sendHTMLEnd();
}

// ==================================================
// SAVE DOOR 2 OPEN
// ==================================================
void handleSaveDoor2Open()
{
    String om = server.arg("openMode");
    String ot = server.arg("openTime");
    int    ol = server.arg("openLightThreshold").toInt();

    strncpy(door2Settings.openMode, om.c_str(), 5); door2Settings.openMode[5] = '\0';
    strncpy(door2Settings.openTime, ot.c_str(), 5); door2Settings.openTime[5] = '\0';
    door2Settings.openLightThreshold = ol;

    saveDoor2Settings();
    applyDoor2SettingsToRam();
    addLog("Klappe2 Öffnungs-Einstellungen gespeichert");
    sendPlain("OK");
}

// ==================================================
// SAVE DOOR 2 CLOSE
// ==================================================
void handleSaveDoor2Close()
{
    String cm = server.arg("closeMode");
    String ct = server.arg("closeTime");
    int    cl = server.arg("closeLightThreshold").toInt();

    strncpy(door2Settings.closeMode, cm.c_str(), 5); door2Settings.closeMode[5] = '\0';
    strncpy(door2Settings.closeTime, ct.c_str(), 5); door2Settings.closeTime[5] = '\0';
    door2Settings.closeLightThreshold = cl;

    door2CloseDelayMin = constrain(server.arg("closeDelayMin").toInt(), 0, 30);
    saveDoor2CloseDelay();

    saveDoor2Settings();
    applyDoor2SettingsToRam();
    addLog("Klappe2 Schließ-Einstellungen gespeichert");
    sendPlain("OK");
}
