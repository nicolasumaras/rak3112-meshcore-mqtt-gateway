#ifndef WEB_UI_H
#define WEB_UI_H

// ---------------------------------------------------------------------------
// Password-protected web UI for MeshCore messaging.
//
//   GET  /            single-page UI
//   GET  /api/state   identity, contacts, messages  (JSON)
//   POST /api/send    {"text": "...", "to": <contact idx or -1 for public>}
//   POST /api/advert  broadcast a signed self-advert
//
// Auth is HTTP Basic against config.security.adminPassword. The page can inject
// traffic into the mesh, so it is refused outright when no password is set
// rather than silently running open.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>

#include "config.h"
#include "meshcore_proto.h"

typedef bool (*WebSendFn)(const uint8_t *data, size_t len);

class MeshWebUI
{
public:
    MeshWebUI(GatewayConfig &cfg, MeshCoreProto &proto, WebSendFn sendFn)
        : server(80), config(cfg), mesh(proto), send(sendFn), started(false) {}

    bool begin()
    {
        if (strlen(config.security.adminPassword) == 0)
        {
            Serial.println(F("⚠ Web UI disabled: no admin password set (menu 10)."));
            Serial.println(F("  The page can transmit into the mesh, so it will not run open."));
            return false;
        }

        server.on("/", HTTP_GET, [this]() { handleRoot(); });
        server.on("/api/state", HTTP_GET, [this]() { handleState(); });
        server.on("/api/send", HTTP_POST, [this]() { handleSend(); });
        server.on("/api/advert", HTTP_POST, [this]() { handleAdvert(); });
        server.onNotFound([this]() { server.send(404, "text/plain", "not found"); });
        server.begin();
        started = true;
        return true;
    }

    void loop()
    {
        if (started) server.handleClient();
    }

    bool isStarted() const { return started; }

private:
    WebServer server;
    GatewayConfig &config;
    MeshCoreProto &mesh;
    WebSendFn send;
    bool started;

    bool authed()
    {
        if (!server.authenticate("admin", config.security.adminPassword))
        {
            server.requestAuthentication();
            return false;
        }
        return true;
    }

    uint32_t nowTs()
    {
        uint32_t ts = (uint32_t)time(nullptr);
        if (ts < 1600000000UL) ts = millis() / 1000;
        return ts;
    }

    void handleRoot()
    {
        if (!authed()) return;
        server.send_P(200, "text/html", PAGE);
    }

    void handleState()
    {
        if (!authed()) return;

        StaticJsonDocument<4096> doc;
        char hex[8];
        snprintf(hex, sizeof(hex), "0x%02X", mesh.selfHash());
        doc["self"] = hex;
        doc["name"] = config.repeater.nodeName;
        snprintf(hex, sizeof(hex), "0x%02X", mesh.channelHash());
        doc["channel"] = hex;

        JsonArray cs = doc.createNestedArray("contacts");
        for (int i = 0; i < MC_MAX_CONTACTS; ++i)
        {
            if (!mesh.contacts[i].used) continue;
            JsonObject o = cs.createNestedObject();
            o["idx"] = i;
            o["name"] = mesh.contacts[i].name;
            o["rssi"] = mesh.contacts[i].lastRssi;
        }

        JsonArray ms = doc.createNestedArray("messages");
        int n = mesh.messageCount();
        if (n > 20) n = 20;
        for (int i = 0; i < n; ++i)
        {
            const MCMessage &m = mesh.messageAt(i);
            JsonObject o = ms.createNestedObject();
            o["from"] = m.from;
            o["text"] = m.text;
            o["rssi"] = m.rssi;
            o["direct"] = m.isDirect;
            o["out"] = m.outgoing;
            o["ts"] = m.timestamp;
        }

        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    }

    void handleSend()
    {
        if (!authed()) return;

        StaticJsonDocument<512> doc;
        if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok)
        {
            server.send(400, "application/json", "{\"error\":\"bad json\"}");
            return;
        }
        const char *text = doc["text"] | "";
        int to = doc["to"] | -1;
        if (strlen(text) == 0)
        {
            server.send(400, "application/json", "{\"error\":\"empty text\"}");
            return;
        }

        uint8_t frame[255];
        size_t n = 0;
        bool direct = false;
        const char *label = "(public)";

        if (to < 0)
        {
            n = mesh.buildGroupText(frame, sizeof(frame), text, nowTs());
        }
        else
        {
            if (to >= MC_MAX_CONTACTS || !mesh.contacts[to].used)
            {
                server.send(400, "application/json", "{\"error\":\"unknown contact\"}");
                return;
            }
            n = mesh.buildDirectText(frame, sizeof(frame), mesh.contacts[to], text, nowTs());
            direct = true;
            label = mesh.contacts[to].name;
        }

        if (n == 0 || !send(frame, n))
        {
            server.send(500, "application/json", "{\"error\":\"transmit failed\"}");
            return;
        }
        mesh.recordOutgoing(label, text, direct);
        server.send(200, "application/json", "{\"ok\":true}");
    }

    void handleAdvert()
    {
        if (!authed()) return;

        // Adverts are replay-protected by timestamp, so the value must strictly
        // increase across reboots. Real time when NTP has synced, otherwise a
        // persisted counter offset past the replay-guard threshold.
        uint32_t ts = (uint32_t)time(nullptr);
        if (ts < 1600000000UL) ts = 1600000000UL + mesh.nextAdvertSeq();

        uint8_t frame[255];
        bool hasLoc = (config.location.latitude != 0.0 || config.location.longitude != 0.0);
        size_t n = mesh.buildAdvert(frame, sizeof(frame), ts, hasLoc,
                                    config.location.latitude, config.location.longitude);
        if (n == 0 || !send(frame, n))
        {
            server.send(500, "application/json", "{\"error\":\"advert failed\"}");
            return;
        }
        server.send(200, "application/json", "{\"ok\":true}");
    }

    static const char PAGE[] PROGMEM;
};

const char MeshWebUI::PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>RAK3112 MeshCore</title><style>
:root{color-scheme:light dark}
body{font:15px/1.5 system-ui,-apple-system,sans-serif;margin:0;padding:1rem;max-width:52rem;margin-inline:auto}
h1{font-size:1.1rem;margin:0 0 .25rem}
.sub{opacity:.65;font-size:.85rem;margin-bottom:1rem}
.card{border:1px solid color-mix(in srgb,currentColor 18%,transparent);border-radius:.6rem;padding:.85rem;margin-bottom:.85rem}
label{display:block;font-size:.8rem;opacity:.7;margin-bottom:.25rem}
select,input,button,textarea{font:inherit;padding:.5rem;border-radius:.4rem;border:1px solid color-mix(in srgb,currentColor 25%,transparent);background:transparent;color:inherit;width:100%;box-sizing:border-box}
button{cursor:pointer;background:color-mix(in srgb,currentColor 10%,transparent);width:auto}
button:hover{background:color-mix(in srgb,currentColor 18%,transparent)}
.row{display:flex;gap:.5rem;align-items:flex-end;flex-wrap:wrap}
.row>*{flex:1 1 12rem}
.row>button{flex:0 0 auto}
ul{list-style:none;padding:0;margin:0}
li{padding:.45rem 0;border-bottom:1px solid color-mix(in srgb,currentColor 10%,transparent)}
li:last-child{border-bottom:0}
.meta{font-size:.75rem;opacity:.6}
.out{opacity:.75}
.tag{font-size:.7rem;border:1px solid currentColor;border-radius:.25rem;padding:0 .3rem;opacity:.7}
.empty{opacity:.5;font-style:italic}
code{font-family:ui-monospace,monospace;font-size:.85em}
</style></head><body>
<h1>RAK3112 &middot; MeshCore</h1>
<div class="sub" id="ident">loading&hellip;</div>

<div class="card">
  <div class="row">
    <div><label for="to">Send to</label><select id="to"><option value="-1">Public channel</option></select></div>
    <div style="flex:2 1 20rem"><label for="msg">Message</label><input id="msg" placeholder="type a message" maxlength="140"></div>
    <button id="send">Send</button>
  </div>
  <div class="meta" id="status" style="margin-top:.5rem"></div>
</div>

<div class="card">
  <div class="row" style="margin-bottom:.5rem">
    <strong style="flex:1">Contacts</strong>
    <button id="adv" title="Broadcast a signed advert so other nodes can add this gateway">Announce me</button>
  </div>
  <ul id="contacts"><li class="empty">none yet &mdash; contacts appear when a node&rsquo;s advert is received</li></ul>
</div>

<div class="card">
  <strong>Messages</strong>
  <ul id="msgs" style="margin-top:.5rem"><li class="empty">nothing received yet</li></ul>
</div>

<script>
const $=s=>document.querySelector(s);
function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
let lastSel=-1;
async function refresh(){
  let d; try{ d=await (await fetch('/api/state')).json() }catch(e){ return }
  $('#ident').innerHTML='<code>'+esc(d.name)+'</code> &middot; node '+esc(d.self)+' &middot; channel '+esc(d.channel);

  const sel=$('#to'); lastSel=sel.value;
  sel.innerHTML='<option value="-1">Public channel</option>';
  d.contacts.forEach(c=>{
    const o=document.createElement('option'); o.value=c.idx;
    o.textContent=c.name+' ('+c.rssi+' dBm)'; sel.appendChild(o);
  });
  sel.value=lastSel;

  const cl=$('#contacts');
  cl.innerHTML = d.contacts.length ? '' : '<li class="empty">none yet &mdash; contacts appear when a node&rsquo;s advert is received</li>';
  d.contacts.forEach(c=>{
    const li=document.createElement('li');
    li.innerHTML=esc(c.name)+' <span class="meta">'+c.rssi+' dBm</span>';
    cl.appendChild(li);
  });

  const ml=$('#msgs');
  ml.innerHTML = d.messages.length ? '' : '<li class="empty">nothing received yet</li>';
  d.messages.forEach(m=>{
    const li=document.createElement('li');
    if(m.out) li.className='out';
    const tag=m.direct?'<span class="tag">direct</span> ':'';
    const dir=m.out?'&rarr; ':'';
    li.innerHTML=tag+dir+'<strong>'+esc(m.from)+'</strong><br>'+esc(m.text)+
      '<div class="meta">'+(m.out?'sent':(m.rssi+' dBm'))+'</div>';
    ml.appendChild(li);
  });
}
async function post(url,body){
  const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body||{})});
  return r.ok ? null : ((await r.json().catch(()=>({}))).error || ('HTTP '+r.status));
}
$('#send').onclick=async()=>{
  const t=$('#msg').value.trim(); if(!t) return;
  $('#status').textContent='sending&hellip;'; $('#send').disabled=true;
  const err=await post('/api/send',{text:t,to:parseInt($('#to').value,10)});
  $('#send').disabled=false;
  $('#status').textContent = err ? ('failed: '+err) : 'sent';
  if(!err) $('#msg').value='';
  refresh();
};
$('#msg').addEventListener('keydown',e=>{ if(e.key==='Enter') $('#send').click() });
$('#adv').onclick=async()=>{
  $('#status').textContent='announcing&hellip;';
  const err=await post('/api/advert');
  $('#status').textContent = err ? ('advert failed: '+err) : 'advert sent — add this node on your other device';
};
refresh(); setInterval(refresh,4000);
</script></body></html>)HTML";

#endif // WEB_UI_H
