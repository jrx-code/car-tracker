// The portal page. Kept in PROGMEM rather than LittleFS: the filesystem holds
// the offline position queue and the CA certificate, and a page that cannot be
// served because the queue filled the partition would defeat its own purpose.
#pragma once
#include <Arduino.h>

static const char PORTAL_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="pl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Car Tracker</title>
<style>
:root{--bg:#12141a;--panel:#1a1d26;--panel2:#222633;--line:#2c313f;--text:#e8eaed;
--muted:#98a0ae;--ok:#5ad18b;--warn:#e3b341;--bad:#ff7b72;--accent:#6cb6ff}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,sans-serif;background:var(--bg);
color:var(--text);padding:14px;max-width:760px;margin:0 auto}
header{display:flex;align-items:center;gap:10px;margin-bottom:14px}
h1{font-size:17px;margin:0}
h2{font-size:14px;margin:18px 0 8px;color:var(--muted);text-transform:uppercase;
letter-spacing:.06em}
.pill{margin-left:auto;font-size:12px;padding:4px 10px;border-radius:999px;
background:var(--panel2);color:var(--muted)}
.pill.ok{color:var(--ok)}.pill.bad{color:var(--bad)}.pill.warn{color:var(--warn)}
table{border-collapse:collapse;width:100%}
td{padding:6px 4px;border-bottom:1px solid var(--line);font-size:14px}
td:first-child{color:var(--muted);width:46%}
button{background:var(--panel2);color:var(--text);border:1px solid var(--line);
border-radius:6px;padding:8px 12px;font-size:14px;cursor:pointer}
button:hover{border-color:var(--accent)}
button.primary{border-color:var(--accent);color:var(--accent)}
button.danger{border-color:var(--bad);color:var(--bad)}
input,select,textarea{width:100%;background:var(--panel);color:var(--text);
border:1px solid var(--line);border-radius:6px;padding:7px 8px;font-size:14px;
font-family:inherit}
textarea{min-height:120px;font-family:ui-monospace,monospace;font-size:12px}
label{display:block;margin:8px 0}
label span{display:block;font-size:12px;color:var(--muted);margin-bottom:3px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:0 14px}
@media(max-width:560px){.grid{grid-template-columns:1fr}}
.card{background:var(--panel);border:1px solid var(--line);border-radius:8px;
padding:12px;margin-bottom:12px}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-top:10px}
.hint{font-size:12px;color:var(--muted);margin-top:4px}
.msg{padding:9px 11px;border-radius:6px;margin:10px 0;font-size:13px;display:none}
.msg.ok{background:#12301f;color:var(--ok);display:block}
.msg.err{background:#331a1a;color:var(--bad);display:block}
.chk{display:flex;align-items:center;gap:8px;margin:8px 0}
.chk input{width:auto}
#admin{display:none}
</style></head><body>

<header>
  <h1>Car Tracker <span id="vname" style="color:var(--muted);font-weight:400"></span></h1>
  <div id="state" class="pill">...</div>
</header>

<div class="card">
  <table id="status"></table>
  <div class="row">
    <button id="toggle" class="primary">Tryb admin</button>
    <button id="refresh">Odswiez</button>
  </div>
</div>

<div id="admin">
  <div class="msg" id="msg"></div>

  <div class="card">
    <h2>Haslo administratora</h2>
    <label><span>Wymagane do zapisu. Odczyt ustawien jest otwarty, hasla nigdy nie wracaja do przegladarki.</span>
      <input type="password" id="auth" placeholder="haslo admina"></label>
  </div>

  <div class="card">
    <h2>Pojazd</h2>
    <div class="grid">
      <label><span>vehicle_id (tworzy temat MQTT)</span><input id="vehicle_id"></label>
      <label><span>Nazwa opisowa</span><input id="vehicle_name"></label>
      <label><span>Hostname / mDNS</span><input id="hostname"></label>
    </div>
  </div>

  <div class="card">
    <h2>WiFi</h2>
    <div class="chk"><input type="checkbox" id="wifi_enabled"><label style="margin:0">Uzywaj WiFi</label></div>
    <div class="grid">
      <label><span>SSID</span>
        <input id="wifi_ssid" list="ssids"><datalist id="ssids"></datalist></label>
      <label><span>Haslo (puste = bez zmiany)</span><input type="password" id="wifi_pass"></label>
      <label><span>Timeout laczenia (s)</span><input type="number" id="wifi_timeout_s"></label>
    </div>
    <div class="row"><button id="scan">Skanuj sieci</button></div>
  </div>

  <div class="card">
    <h2>AP awaryjny</h2>
    <div class="hint">Gdy urzadzenie jest offline dluzej niz ponizszy czas, samo stawia
      wlasna siec. To jedyna droga do trackera pod deska, gdy WiFi i LTE nie dzialaja.</div>
    <div class="chk"><input type="checkbox" id="ap_enabled"><label style="margin:0">Wlaczony</label></div>
    <div class="grid">
      <label><span>SSID (puste = cartracker-&lt;id&gt;)</span><input id="ap_ssid"></label>
      <label><span>Haslo (min. 8 znakow, puste = otwarta)</span><input type="password" id="ap_pass"></label>
      <label><span>Podnies po (s offline)</span><input type="number" id="ap_after_s"></label>
      <label><span>Wylacz po (s, 0 = nigdy)</span><input type="number" id="ap_timeout_s"></label>
    </div>
    <div class="row"><button id="ap_now">Podnies AP teraz</button></div>
  </div>

  <div class="card">
    <h2>MQTT</h2>
    <div class="grid">
      <label><span>Host</span><input id="mqtt_host"></label>
      <label><span>Port</span><input type="number" id="mqtt_port"></label>
      <label><span>Uzytkownik</span><input id="mqtt_user"></label>
      <label><span>Haslo (puste = bez zmiany)</span><input type="password" id="mqtt_pass"></label>
      <label><span>Prefiks tematow</span><input id="topic_prefix"></label>
      <label><span>Keepalive (s)</span><input type="number" id="mqtt_keepalive"></label>
    </div>
    <div class="chk"><input type="checkbox" id="mqtt_tls"><label style="margin:0">TLS</label></div>
    <div class="chk"><input type="checkbox" id="mqtt_verify_ca"><label style="margin:0">Weryfikuj certyfikat brokera</label></div>
    <label><span>Certyfikat CA (PEM). Puste pole nie kasuje zapisanego.</span>
      <textarea id="ca" placeholder="-----BEGIN CERTIFICATE-----"></textarea></label>
    <div class="row">
      <button id="save_ca">Zapisz certyfikat</button>
      <button id="del_ca" class="danger">Usun certyfikat</button>
      <span id="ca_state" class="hint"></span>
    </div>
  </div>

  <div class="card">
    <h2>LTE</h2>
    <div class="chk"><input type="checkbox" id="modem_enabled"><label style="margin:0">Modem wlaczony</label></div>
    <div class="grid">
      <label><span>APN</span><input id="apn"></label>
      <label><span>APN user</span><input id="apn_user"></label>
      <label><span>APN haslo (puste = bez zmiany)</span><input type="password" id="apn_pass"></label>
      <label><span>PIN SIM (puste = bez zmiany)</span><input type="password" id="sim_pin"></label>
    </div>
    <div class="chk"><input type="checkbox" id="allow_roaming"><label style="margin:0">Zezwalaj na roaming</label></div>
  </div>

  <div class="card">
    <h2>Piny</h2>
    <div class="hint">GPIO 6-11 sa odrzucane (obsluguja pamiec flash). -1 znaczy brak.</div>
    <div class="grid">
      <label><span>GNSS RX (wejscie ESP)</span><input type="number" id="pin_gnss_rx"></label>
      <label><span>GNSS TX</span><input type="number" id="pin_gnss_tx"></label>
      <label><span>GNSS zasilanie (load switch)</span><input type="number" id="pin_gnss_en"></label>
      <label><span>GNSS baud</span><input type="number" id="gnss_baud"></label>
      <label><span>Modem RX</span><input type="number" id="pin_modem_rx"></label>
      <label><span>Modem TX</span><input type="number" id="pin_modem_tx"></label>
      <label><span>Modem PWRKEY</span><input type="number" id="pin_modem_pwrkey"></label>
      <label><span>Modem zasilanie</span><input type="number" id="pin_modem_en"></label>
      <label><span>Pomiar napiecia (ADC)</span><input type="number" id="pin_vbat_adc"></label>
      <label><span>Akcelerometr INT</span><input type="number" id="pin_acc_int"></label>
      <label><span>I2C SDA</span><input type="number" id="pin_i2c_sda"></label>
      <label><span>I2C SCL</span><input type="number" id="pin_i2c_scl"></label>
      <label><span>Dioda</span><input type="number" id="pin_led"></label>
    </div>
  </div>

  <div class="card">
    <h2>Portal i serwis</h2>
    <div class="chk"><input type="checkbox" id="ota_enabled"><label style="margin:0">OTA wlaczone</label></div>
    <label><span>Nowe haslo admina (puste = bez zmiany)</span><input type="password" id="admin_pass"></label>
    <div class="row">
      <button id="save" class="primary">Zapisz ustawienia</button>
      <button id="reboot">Restart</button>
      <button id="factory" class="danger">Ustawienia fabryczne</button>
    </div>
  </div>
</div>

<script>
let adminOn=false;

async function api(path,opts){const r=await fetch(path,opts);
  const t=await r.text();let d;try{d=JSON.parse(t)}catch(e){d={error:t}}
  if(!r.ok)throw new Error(d.error||r.status);return d}

function msg(text,ok){const m=document.getElementById('msg');
  m.textContent=text;m.className='msg '+(ok?'ok':'err');
  setTimeout(()=>{m.className='msg'},6000)}

function row(k,v){return '<tr><td>'+k+'</td><td>'+v+'</td></tr>'}

async function loadStatus(){
  try{
    const s=await api('/api/status');
    document.getElementById('vname').textContent=s.vehicle_name?('— '+s.vehicle_name):'';
    const p=document.getElementById('state');
    p.textContent=s.ap?('AP: '+s.ap_ssid):(s.sta?('WiFi '+s.rssi+' dBm'):'offline');
    p.className='pill '+(s.ap?'warn':(s.sta?'ok':'bad'));
    let h='';
    h+=row('Tryb',s.mode);
    h+=row('Pozycja',s.fix?(s.lat.toFixed(6)+', '+s.lon.toFixed(6)):'brak fixa');
    h+=row('Satelity / HDOP',s.sat+' / '+(s.hdop!==undefined?s.hdop:'-'));
    h+=row('Napiecie',s.vbat!==undefined?(s.vbat.toFixed(2)+' V'):'-');
    h+=row('MQTT',s.mqtt?'polaczony':'rozlaczony');
    h+=row('Kolejka offline',s.queued+' punktow');
    h+=row('Adres',s.ip);
    h+=row('Firmware',s.fw);
    h+=row('Uptime',Math.floor(s.uptime/60)+' min');
    document.getElementById('status').innerHTML=h;
  }catch(e){document.getElementById('state').textContent='blad';}
}

async function loadSettings(){
  const s=await api('/api/settings');
  for(const k in s){
    const el=document.getElementById(k);
    if(!el)continue;
    if(el.type==='checkbox')el.checked=!!s[k];else el.value=s[k];
  }
  document.getElementById('ca_state').textContent=
    s.ca_present?'certyfikat zapisany':'brak certyfikatu';
}

function collect(){
  const out={};
  document.querySelectorAll('#admin input,#admin select,#admin textarea').forEach(el=>{
    if(!el.id||el.id==='auth'||el.id==='ca')return;
    if(el.type==='checkbox')out[el.id]=el.checked;
    else if(el.type==='number')out[el.id]=el.value===''?undefined:Number(el.value);
    else out[el.id]=el.value;
  });
  Object.keys(out).forEach(k=>out[k]===undefined&&delete out[k]);
  return out;
}

function authHeaders(){
  return {'Content-Type':'application/json',
          'X-Admin-Pass':document.getElementById('auth').value};
}

document.getElementById('toggle').onclick=async()=>{
  adminOn=!adminOn;
  document.getElementById('admin').style.display=adminOn?'block':'none';
  document.getElementById('toggle').textContent=adminOn?'Zamknij tryb admin':'Tryb admin';
  if(adminOn){try{await loadSettings()}catch(e){msg('Nie udalo sie wczytac ustawien: '+e.message,false)}}
};
document.getElementById('refresh').onclick=loadStatus;

document.getElementById('save').onclick=async()=>{
  try{
    await api('/api/settings',{method:'POST',headers:authHeaders(),
      body:JSON.stringify(collect())});
    msg('Zapisane. Czesc zmian (WiFi, piny, MQTT) dziala po restarcie.',true);
    await loadSettings();
  }catch(e){msg('Odrzucone: '+e.message,false)}
};

document.getElementById('save_ca').onclick=async()=>{
  try{
    const pem=document.getElementById('ca').value.trim();
    if(!pem){msg('Pole certyfikatu jest puste. Do usuniecia sluzy przycisk obok.',false);return}
    await api('/api/ca',{method:'POST',headers:authHeaders(),body:pem});
    msg('Certyfikat zapisany.',true);await loadSettings();
  }catch(e){msg('Odrzucone: '+e.message,false)}
};
document.getElementById('del_ca').onclick=async()=>{
  if(!confirm('Usunac zapisany certyfikat CA?'))return;
  try{await api('/api/action',{method:'POST',headers:authHeaders(),
      body:JSON.stringify({action:'delete_ca'})});
    msg('Certyfikat usuniety.',true);await loadSettings();
  }catch(e){msg('Odrzucone: '+e.message,false)}
};

document.getElementById('scan').onclick=async()=>{
  try{
    const r=await api('/api/scan');
    const dl=document.getElementById('ssids');dl.innerHTML='';
    r.networks.forEach(n=>{const o=document.createElement('option');
      o.value=n.ssid;o.label=n.rssi+' dBm';dl.appendChild(o)});
    msg('Znaleziono '+r.networks.length+' sieci. Lista jest w polu SSID.',true);
  }catch(e){msg('Skan nieudany: '+e.message,false)}
};

async function action(name,confirmText){
  if(confirmText&&!confirm(confirmText))return;
  try{await api('/api/action',{method:'POST',headers:authHeaders(),
    body:JSON.stringify({action:name})});
    msg('Wykonane: '+name,true);
  }catch(e){msg('Odrzucone: '+e.message,false)}
}
document.getElementById('reboot').onclick=()=>action('reboot','Zrestartowac urzadzenie?');
document.getElementById('factory').onclick=()=>action('factory_reset',
  'Skasowac WSZYSTKIE ustawienia i wrocic do fabrycznych? Tego nie da sie cofnac.');
document.getElementById('ap_now').onclick=()=>action('start_ap');

loadStatus();
setInterval(loadStatus,5000);
</script>
</body></html>
)HTML";
