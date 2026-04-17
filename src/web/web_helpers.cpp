#include "web.h"
#include "../storage.h"

// ==================================================
// ANTWORT-HELPER
// ==================================================
// setNoDelay(true) = Nagle-Algorithmus deaktivieren.
// Nagle puffert kleine TCP-Segmente ~200ms → verursacht
// den sichtbaren "Hänger" im iOS Safari Ladebalken.

void sendHTML(const String &html, int code)
{
    server.client().setNoDelay(true);
    server.sendHeader("Connection", "close");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(code, "text/html; charset=UTF-8", html);
}

void sendJSON(const String &json, int code)
{
    server.client().setNoDelay(true);
    server.sendHeader("Connection", "close");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(code, "application/json", json);
}

void sendPlain(const String &text, int code)
{
    server.client().setNoDelay(true);
    server.sendHeader("Connection", "close");
    server.send(code, "text/plain; charset=UTF-8", text);
}

// --- Chunked Transfer: Seite stückweise senden ---
// Browser bekommt sofort den <head> (CSS, viewport) und kann
// rendern, während der ESP32 den Rest der Seite aufbaut.
void sendHTMLStart(int code)
{
    server.client().setNoDelay(true);
    server.sendHeader("Connection", "close");
    server.sendHeader("Cache-Control", "no-cache");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(code, "text/html; charset=UTF-8", "");
}

void sendHTMLChunk(const String &chunk)
{
    server.sendContent(chunk);
}

void sendHTMLEnd()
{
    server.sendContent("");
}

// ==================================================
// GEMEINSAME HTML-FRAGMENTE
// ==================================================

String renderThemeHead(const String &title)
{
    return String(R"rawliteral(<!DOCTYPE html>
<html lang="de" data-theme=")rawliteral") + uiTheme + R"rawliteral(">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="Hühnerklappe">
<meta name="theme-color" content="#1e293b">
<link rel="manifest" href="/manifest.json">
<link rel="apple-touch-icon" href="/apple-touch-icon.png">
<link rel="apple-touch-icon-precomposed" href="/apple-touch-icon.png">
<title>)rawliteral" + title + R"rawliteral(</title>
<style>
:root { --bg:#f4f6f9; --card:#ffffff; --text:#1f2937; --muted:#6b7280; --green:#22c55e; --red:#ef4444; --orange:#f59e0b; --radius:18px; }
@media (prefers-color-scheme: dark) { :root[data-theme="auto"] { --bg:#0f172a; --card:#1e293b; --text:#f1f5f9; --muted:#94a3b8; } }
:root[data-theme="dark"]  { --bg:#0f172a; --card:#1e293b; --text:#f1f5f9; --muted:#94a3b8; }
:root[data-theme="light"] { --bg:#f4f6f9; --card:#ffffff; --text:#1f2937; --muted:#6b7280; }
*,*::before,*::after { touch-action:manipulation; -webkit-tap-highlight-color:rgba(0,0,0,0.1); }
body { margin:0; font-family:system-ui,-apple-system; background:var(--bg); color:var(--text); padding-top:env(safe-area-inset-top); -webkit-text-size-adjust:100%; }
.header { text-align:center; padding:26px 0 12px; }
.header h1,.header h2,.header h3 { margin:0; font-weight:700; }
.container { max-width:430px; margin:auto; padding:20px 16px 110px; }
.card { background:var(--card); padding:18px; border-radius:var(--radius); margin-bottom:16px; box-shadow:0 6px 18px rgba(0,0,0,0.06); }
button,.btn-link { width:100%; padding:14px; border:none; border-radius:14px; font-size:16px; font-weight:600; background:var(--green); color:white; margin-top:12px; cursor:pointer; display:block; text-align:center; text-decoration:none; box-sizing:border-box; }
button:active,.btn-link:active { opacity:0.7; transform:scale(0.98); }
a      { touch-action:manipulation; }
button.danger { background:var(--red); }
nav { position:fixed; bottom:18px; left:50%; transform:translateX(-50%); width:100%; max-width:430px; background:var(--card); border-radius:22px; box-shadow:0 10px 25px rgba(0,0,0,0.15); display:flex; padding:0; padding-bottom:env(safe-area-inset-bottom); }
nav a { flex:1; text-align:center; text-decoration:none; color:var(--muted); font-size:13px; font-weight:600; padding:12px 0; -webkit-tap-highlight-color:rgba(0,0,0,0.1); }
</style>
<script>
function toggleTheme(){
  let c=document.documentElement.getAttribute("data-theme");
  let n=c==="auto"?"dark":c==="dark"?"light":"auto";
  fetch("/set-theme",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"theme="+n})
    .then(()=>document.documentElement.setAttribute("data-theme",n));
}
// SPA-Navigation: fetch() nutzt die bestehende TCP-Verbindung (kein iOS WiFi-Stall)
document.addEventListener('click',function(e){
  var a=e.target.closest('a[href^="/"]');
  if(!a||a.getAttribute('onclick'))return;
  e.preventDefault();
  var url=a.getAttribute('href');
  var c=document.querySelector('.container');
  if(c)c.style.opacity='0.4';
  fetch(url,{cache:'no-store'}).then(function(r){return r.text();}).then(function(h){
    document.open();document.write(h);document.close();
    if(history.pushState)history.pushState({},'',url);
  }).catch(function(){location.href=url;});
});
window.addEventListener('popstate',function(){location.reload();});
</script>
</head>
<body>
<div class="container">
)rawliteral";
}

String renderFooter()
{
    return R"rawliteral(
</div>
<nav>
  <a href="/">🏠<br>Start</a>
  <a href="/settings">⚙️<br>Einstellungen</a>
  <a href="/advanced">🔧<br>Erweitert</a>
</nav>
</body>
</html>
)rawliteral";
}

// timeToMinutes() ist in logic.cpp definiert und über web.h deklariert
