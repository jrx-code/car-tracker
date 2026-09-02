// The portal page. Kept in PROGMEM rather than LittleFS: the filesystem holds
// the offline position queue and the CA certificate, and a page that cannot be
// served because the queue filled the partition would defeat its own purpose.
//
// One page, no navigation. Sections collapse, and which ones are open is
// remembered in the browser, so the sequence "open the panel, find the field,
// fix it" stays short even on a phone standing next to the car.
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
body{margin:0 auto;max-width:820px;padding:12px 14px 90px;
font-family:system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--text)}
header{display:flex;align-items:center;gap:10px;padding:6px 0 12px;flex-wrap:wrap}
h1{font-size:17px;margin:0}
.sub{color:var(--muted);font-weight:400}
.pill{font-size:12px;padding:4px 10px;border-radius:999px;background:var(--panel2);
color:var(--muted);white-space:nowrap}
.pill.ok{color:var(--ok)}.pill.bad{color:var(--bad)}.pill.warn{color:var(--warn)}
.pills{display:flex;gap:6px;margin-left:auto;flex-wrap:wrap}

/* status */
.stat{display:grid;grid-template-columns:repeat(auto-fit,minmax(128px,1fr));gap:8px;
background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:12px}
.stat div{min-width:0}
.stat .k{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:.05em}
.stat .v{font-size:15px;margin-top:2px;overflow-wrap:anywhere}

/* sections */
details{background:var(--panel);border:1px solid var(--line);border-radius:10px;
margin-top:10px;overflow:hidden}
details[open]{border-color:#39405224}
summary{cursor:pointer;padding:12px 14px;font-size:14px;font-weight:600;
display:flex;align-items:center;gap:9px;list-style:none;user-select:none}
summary::-webkit-details-marker{display:none}
summary::before{content:"▸";color:var(--muted);transition:transform .15s;font-size:13px}
details[open] summary::before{transform:rotate(90deg)}
summary .note{margin-left:auto;font-size:12px;color:var(--muted);font-weight:400}
summary .bad{color:var(--bad)}
.body{padding:2px 14px 14px}
.lead{font-size:12.5px;color:var(--muted);margin:0 0 12px;line-height:1.45}

/* fields */
.grid{display:grid;grid-template-columns:1fr 1fr;gap:0 16px}
@media(max-width:620px){.grid{grid-template-columns:1fr}}
label{display:block;margin:9px 0}
label>.name{display:block;font-size:13px;margin-bottom:3px}
label>.help{display:block;font-size:11.5px;color:var(--muted);margin-bottom:4px;line-height:1.4}
input,select,textarea{width:100%;background:var(--bg);color:var(--text);
border:1px solid var(--line);border-radius:6px;padding:8px;font-size:14px;
font-family:inherit}
input:focus,textarea:focus,select:focus{outline:none;border-color:var(--accent)}
input.err,textarea.err{border-color:var(--bad)}
.err-msg{display:none;font-size:11.5px;color:var(--bad);margin-top:3px}
input.err+.err-msg,textarea.err+.err-msg{display:block}
textarea{min-height:110px;font-family:ui-monospace,monospace;font-size:12px}
.chk{display:flex;align-items:flex-start;gap:9px;margin:11px 0}
.chk input{width:auto;margin-top:2px}
.chk .t{font-size:13px}
.chk .help{font-size:11.5px;color:var(--muted);line-height:1.4}
button{background:var(--panel2);color:var(--text);border:1px solid var(--line);
border-radius:7px;padding:9px 13px;font-size:14px;cursor:pointer}
button:hover{border-color:var(--accent)}
button.primary{border-color:var(--accent);color:var(--accent)}
button.danger{border-color:var(--bad);color:var(--bad)}
button:disabled{opacity:.45;cursor:not-allowed}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-top:11px}
.hint{font-size:11.5px;color:var(--muted)}

/* save bar */
.bar{position:fixed;left:0;right:0;bottom:0;background:#161923f2;
border-top:1px solid var(--line);padding:10px 14px;display:none;
backdrop-filter:blur(6px)}
.bar.on{display:block}
.bar .in{max-width:820px;margin:0 auto;display:flex;gap:9px;align-items:center;
flex-wrap:wrap}
.bar input{max-width:190px}
.msg{padding:9px 11px;border-radius:7px;margin:10px 0;font-size:13px;display:none;
line-height:1.45}
.msg.ok{background:#12301f;color:var(--ok);display:block}
.msg.err{background:#331a1a;color:var(--bad);display:block}
#admin{display:none}
</style></head><body>

<header>
  <h1>Car Tracker <span class="sub" id="vname"></span></h1>
  <div class="pills">
    <span id="p_net" class="pill">...</span>
    <span id="p_mqtt" class="pill">MQTT</span>
    <span id="p_gps" class="pill">GPS</span>
  </div>
</header>

<div class="stat" id="status"></div>

<div class="row">
  <button id="toggle" class="primary">Tryb admin</button>
  <button id="refresh">Odswiez</button>
  <span class="hint" id="last"></span>
</div>

<div id="admin">
  <div class="msg" id="msg"></div>

  <details id="s_vehicle"><summary>Pojazd <span class="note" id="n_vehicle"></span></summary>
    <div class="body">
      <p class="lead">Identyfikator tworzy temat MQTT. Musi byc taki sam jak wpis
        w Home Assistant i w agregatorze, inaczej dane trafia w prozne.</p>
      <div class="grid">
        <label><span class="name">Identyfikator pojazdu</span>
          <span class="help">Litery, cyfry, - i _. Np. nd1</span>
          <input id="vehicle_id"><span class="err-msg"></span></label>
        <label><span class="name">Nazwa opisowa</span>
          <span class="help">Tylko do wyswietlania w tym panelu</span>
          <input id="vehicle_name"></label>
        <label><span class="name">Hostname / mDNS</span>
          <span class="help">Pod ta nazwa urzadzenie jest widoczne w sieci</span>
          <input id="hostname"></label>
      </div>
    </div>
  </details>

  <details id="s_wifi"><summary>WiFi <span class="note" id="n_wifi"></span></summary>
    <div class="body">
      <p class="lead">Siec uzywana na warsztacie, w garazu i do aktualizacji.
        W aucie zastapi ja modem LTE, ale WiFi zostaje jako druga droga.</p>
      <div class="chk"><input type="checkbox" id="wifi_enabled">
        <div><div class="t">Uzywaj WiFi</div>
          <div class="help">Wylacz tylko gdy urzadzenie ma dzialac wylacznie na LTE</div></div></div>
      <div class="grid">
        <label><span class="name">SSID</span>
          <span class="help">Kliknij „Skanuj sieci”, zeby wybrac z listy</span>
          <input id="wifi_ssid" list="ssids"><datalist id="ssids"></datalist>
          <span class="err-msg"></span></label>
        <label><span class="name">Haslo</span>
          <span class="help">Puste = zostaw obecne bez zmiany</span>
          <input type="password" id="wifi_pass"></label>
        <label><span class="name">Timeout laczenia (s)</span>
          <span class="help">Po tym czasie urzadzenie uznaje sie za offline</span>
          <input type="number" id="wifi_timeout_s" min="5" max="300">
          <span class="err-msg"></span></label>
      </div>
      <div class="row"><button id="scan">Skanuj sieci</button>
        <span class="hint" id="scan_out"></span></div>
    </div>
  </details>

  <details id="s_ap"><summary>AP awaryjny <span class="note" id="n_ap"></span></summary>
    <div class="body">
      <p class="lead">Gdy urzadzenie jest offline dluzej niz ponizszy czas, samo
        stawia wlasna siec WiFi. To jedyna droga do trackera pod deska rozdzielcza,
        gdy haslo do WiFi jest bledne albo karta SIM przestala dzialac. Po powrocie
        do normalnej sieci AP jest zwijany.</p>
      <div class="chk"><input type="checkbox" id="ap_enabled">
        <div><div class="t">AP awaryjny wlaczony</div>
          <div class="help">Wylaczenie zostawia urzadzenie bez zapasowego wejscia</div></div></div>
      <div class="grid">
        <label><span class="name">SSID</span>
          <span class="help">Puste = cartracker-&lt;identyfikator&gt;</span>
          <input id="ap_ssid"></label>
        <label><span class="name">Haslo</span>
          <span class="help">Minimum 8 znakow. Puste = siec otwarta</span>
          <input type="password" id="ap_pass"><span class="err-msg"></span></label>
        <label><span class="name">Podnies po (s offline)</span>
          <span class="help">Za krotki czas = AP wstaje przy kazdym mrugnieciu sieci</span>
          <input type="number" id="ap_after_s" min="30" max="3600">
          <span class="err-msg"></span></label>
        <label><span class="name">Zwin po (s)</span>
          <span class="help">0 = nigdy. AP wiszacy dobe w aucie to pobor pradu</span>
          <input type="number" id="ap_timeout_s" min="0" max="86400">
          <span class="err-msg"></span></label>
      </div>
      <div class="row"><button id="ap_now">Podnies AP teraz</button></div>
    </div>
  </details>

  <details id="s_mqtt"><summary>MQTT <span class="note" id="n_mqtt"></span></summary>
    <div class="body">
      <p class="lead">Broker, do ktorego ida pozycje i telemetria. Konto powinno byc
        osobne dla kazdego pojazdu.</p>
      <div class="grid">
        <label><span class="name">Host</span><span class="help">Np. mqtt.example.lan</span>
          <input id="mqtt_host"><span class="err-msg"></span></label>
        <label><span class="name">Port</span><span class="help">8883 dla TLS, 1883 bez</span>
          <input type="number" id="mqtt_port" min="1" max="65535">
          <span class="err-msg"></span></label>
        <label><span class="name">Uzytkownik</span><span class="help">Np. cartracker-nd1</span>
          <input id="mqtt_user"></label>
        <label><span class="name">Haslo</span><span class="help">Puste = bez zmiany</span>
          <input type="password" id="mqtt_pass"></label>
        <label><span class="name">Prefiks tematow</span>
          <span class="help">Tematy to &lt;prefiks&gt;/&lt;identyfikator&gt;/...</span>
          <input id="topic_prefix"></label>
        <label><span class="name">Keepalive (s)</span>
          <span class="help">Po tylu sekundach ciszy broker oglosi urzadzenie offline</span>
          <input type="number" id="mqtt_keepalive" min="10" max="600">
          <span class="err-msg"></span></label>
      </div>
      <div class="chk"><input type="checkbox" id="mqtt_tls">
        <div><div class="t">TLS</div><div class="help">Szyfrowane polaczenie z brokerem</div></div></div>
      <div class="chk"><input type="checkbox" id="mqtt_verify_ca">
        <div><div class="t">Weryfikuj certyfikat brokera</div>
          <div class="help">Wylaczenie zostawia szyfrowanie, ale bez sprawdzenia
            z kim urzadzenie rozmawia. Do warsztatu, nie do auta</div></div></div>
      <label><span class="name">Certyfikat CA (PEM)</span>
        <span class="help">Wez kotwice z lancucha, ktory serwer realnie wysyla:
          <code>openssl s_client -connect host:8883 -showcerts</code>, ostatni certyfikat.
          Puste pole nic nie kasuje, do usuniecia sluzy przycisk obok.</span>
        <textarea id="ca" placeholder="-----BEGIN CERTIFICATE-----"></textarea>
        <span class="err-msg"></span></label>
      <div class="row">
        <button id="save_ca">Zapisz certyfikat</button>
        <button id="del_ca" class="danger">Usun certyfikat</button>
        <span class="hint" id="ca_state"></span>
      </div>
    </div>
  </details>

  <details id="s_lte"><summary>LTE <span class="note" id="n_lte"></span></summary>
    <div class="body">
      <p class="lead">Uzywane dopiero po zamontowaniu modemu. Na czas testow
        na WiFi mozna zostawic wylaczone.</p>
      <div class="chk"><input type="checkbox" id="modem_enabled">
        <div><div class="t">Modem wlaczony</div>
          <div class="help">Wylaczony modem nie jest zasilany, co oszczedza prad</div></div></div>
      <div class="grid">
        <label><span class="name">APN</span><span class="help">Zalezny od operatora karty SIM</span>
          <input id="apn"></label>
        <label><span class="name">Uzytkownik APN</span><span class="help">Zwykle pusty</span>
          <input id="apn_user"></label>
        <label><span class="name">Haslo APN</span><span class="help">Puste = bez zmiany</span>
          <input type="password" id="apn_pass"></label>
        <label><span class="name">PIN karty SIM</span>
          <span class="help">Najlepiej wylaczyc PIN na karcie. Trzy bledne proby to wyjazd do auta</span>
          <input type="password" id="sim_pin"></label>
      </div>
      <div class="chk"><input type="checkbox" id="allow_roaming">
        <div><div class="t">Zezwalaj na roaming</div>
          <div class="help">Potrzebne przy wyjazdach za granice</div></div></div>
    </div>
  </details>

  <details id="s_pins"><summary>Piny <span class="note" id="n_pins"></span></summary>
    <div class="body">
      <p class="lead">GPIO 6-11 sa odrzucane, bo obsluguja pamiec flash i sterowanie
        nimi uniemozliwia start. Wartosc -1 znaczy „niepodlaczone”. Zmiana pinow
        wymaga restartu.</p>
      <div class="grid">
        <label><span class="name">GNSS TX modulu &rarr; ten pin</span>
          <span class="help">Wejscie ESP32. Tu podlacza sie TX modulu GPS</span>
          <input type="number" id="pin_gnss_rx"><span class="err-msg"></span></label>
        <label><span class="name">GNSS RX modulu &larr; ten pin</span>
          <span class="help">Wyjscie ESP32, w praktyce nieuzywane</span>
          <input type="number" id="pin_gnss_tx"><span class="err-msg"></span></label>
        <label><span class="name">Zasilanie GNSS</span>
          <span class="help">Load switch odcinajacy modul na postoju</span>
          <input type="number" id="pin_gnss_en"><span class="err-msg"></span></label>
        <label><span class="name">Predkosc GNSS (baud)</span>
          <span class="help">u-blox domyslnie 9600</span>
          <input type="number" id="gnss_baud"><span class="err-msg"></span></label>
        <label><span class="name">Modem RX</span><span class="help">Wejscie ESP32</span>
          <input type="number" id="pin_modem_rx"><span class="err-msg"></span></label>
        <label><span class="name">Modem TX</span><span class="help">Wyjscie ESP32</span>
          <input type="number" id="pin_modem_tx"><span class="err-msg"></span></label>
        <label><span class="name">Modem PWRKEY</span><span class="help">Impuls wlaczajacy modem</span>
          <input type="number" id="pin_modem_pwrkey"><span class="err-msg"></span></label>
        <label><span class="name">Zasilanie modemu</span><span class="help">Load switch</span>
          <input type="number" id="pin_modem_en"><span class="err-msg"></span></label>
        <label><span class="name">Pomiar napiecia (ADC)</span>
          <span class="help">Dzielnik z pinu 16 gniazda OBD</span>
          <input type="number" id="pin_vbat_adc"><span class="err-msg"></span></label>
        <label><span class="name">Akcelerometr INT</span>
          <span class="help">Musi byc pinem RTC, zeby wybudzal z uspienia</span>
          <input type="number" id="pin_acc_int"><span class="err-msg"></span></label>
        <label><span class="name">I2C SDA</span><span class="help">Akcelerometr</span>
          <input type="number" id="pin_i2c_sda"><span class="err-msg"></span></label>
        <label><span class="name">I2C SCL</span><span class="help">Akcelerometr</span>
          <input type="number" id="pin_i2c_scl"><span class="err-msg"></span></label>
        <label><span class="name">Dioda</span><span class="help">Sygnalizacja stanu</span>
          <input type="number" id="pin_led"><span class="err-msg"></span></label>
      </div>
    </div>
  </details>

  <details id="s_svc"><summary>Portal i serwis <span class="note" id="n_svc"></span></summary>
    <div class="body">
      <p class="lead">Haslo administratora chroni zapis ustawien. Odczyt statusu
        jest otwarty celowo, zeby dalo sie zdiagnozowac urzadzenie stojac przy aucie.</p>
      <div class="chk"><input type="checkbox" id="ota_enabled">
        <div><div class="t">Aktualizacja przez siec (OTA)</div>
          <div class="help">Pozwala wgrac firmware bez kabla</div></div></div>
      <label><span class="name">Nowe haslo administratora</span>
        <span class="help">Puste = bez zmiany. Zapamietaj je, bo bez niego zostaje
          tylko przywrocenie ustawien fabrycznych</span>
        <input type="password" id="admin_pass"><span class="err-msg"></span></label>
      <div class="row">
        <button id="reboot">Restart</button>
        <button id="factory" class="danger">Ustawienia fabryczne</button>
      </div>
    </div>
  </details>
</div>

<div class="bar" id="bar"><div class="in">
  <input type="password" id="auth" placeholder="haslo admina">
  <button id="save" class="primary">Zapisz ustawienia</button>
  <span class="hint" id="bar_hint">Zmiany nie sa zapisane</span>
</div></div>

<script>
const $=id=>document.getElementById(id);
let adminOn=false, dirty=false;

async function api(p,o){const r=await fetch(p,o);const t=await r.text();
  let d;try{d=JSON.parse(t)}catch(e){d={error:t}}
  if(!r.ok)throw new Error(d.error||('HTTP '+r.status));return d}

function msg(t,ok){const m=$('msg');m.textContent=t;m.className='msg '+(ok?'ok':'err');
  m.scrollIntoView({block:'nearest',behavior:'smooth'});
  setTimeout(()=>{m.className='msg'},8000)}

// --- validation, mirrors the rules the firmware enforces -------------------
const PIN_HELP='GPIO 0-39, bez 6-11 (pamiec flash). -1 = niepodlaczone';
const RULES={
  vehicle_id:v=>!v?'Nie moze byc puste, tworzy temat MQTT':
    (/^[A-Za-z0-9_-]+$/.test(v)?(v.length<16?'':'Maksymalnie 15 znakow')
     :'Dozwolone litery, cyfry, - i _'),
  mqtt_host:v=>v?'':'Nie moze byc puste',
  mqtt_port:v=>(v>=1&&v<=65535)?'':'Zakres 1-65535',
  mqtt_keepalive:v=>(v>=10&&v<=600)?'':'Zakres 10-600 s',
  wifi_timeout_s:v=>(v>=5&&v<=300)?'':'Zakres 5-300 s',
  ap_pass:v=>(!v||v.length>=8)?'':'Minimum 8 znakow albo puste (siec otwarta)',
  ap_after_s:v=>(v>=30&&v<=3600)?'':'Zakres 30-3600 s',
  ap_timeout_s:v=>(v===0||(v>=60&&v<=86400))?'':'0 albo 60-86400 s',
  gnss_baud:v=>[4800,9600,19200,38400,57600,115200].includes(v)?'':
    'Typowe: 4800, 9600, 19200, 38400, 57600, 115200',
};
const PIN_IDS=['pin_gnss_rx','pin_gnss_tx','pin_gnss_en','pin_modem_rx','pin_modem_tx',
  'pin_modem_pwrkey','pin_modem_en','pin_vbat_adc','pin_acc_int','pin_i2c_sda',
  'pin_i2c_scl','pin_led'];
PIN_IDS.forEach(id=>{RULES[id]=v=>{
  if(v===-1)return id==='pin_gnss_rx'?'Wejscie GNSS musi byc podlaczone':'';
  if(v<0||v>39)return PIN_HELP;
  if(v>=6&&v<=11)return 'GPIO 6-11 obsluguja pamiec flash';
  return '';}});

function fieldValue(el){
  if(el.type==='checkbox')return el.checked;
  if(el.type==='number')return el.value===''?null:Number(el.value);
  return el.value;
}

function validateField(el){
  const rule=RULES[el.id];
  const box=el.parentElement.querySelector('.err-msg');
  let err='';
  if(rule){const v=fieldValue(el);if(v!==null&&v!==undefined)err=rule(v)||''}
  el.classList.toggle('err',!!err);
  if(box)box.textContent=err;
  return !err;
}

function validateAll(){
  let ok=true;
  const errBySection={};
  document.querySelectorAll('#admin input,#admin textarea').forEach(el=>{
    if(!el.id||el.id==='auth'||el.id==='ca')return;
    if(!validateField(el)){
      ok=false;
      const sec=el.closest('details');
      if(sec)errBySection[sec.id]=(errBySection[sec.id]||0)+1;
    }
  });
  // Cross-field rule, same one the firmware refuses on.
  const rx=$('pin_gnss_rx'),tx=$('pin_gnss_tx');
  if(rx.value!==''&&rx.value===tx.value){
    rx.classList.add('err');
    rx.parentElement.querySelector('.err-msg').textContent='RX i TX nie moga byc tym samym pinem';
    ok=false;
    errBySection['s_pins']=(errBySection['s_pins']||0)+1;
  }
  document.querySelectorAll('#admin details').forEach(d=>{
    const note=d.querySelector('.note');
    const n=errBySection[d.id];
    note.textContent=n?(n+(n===1?' blad':' bledy')):'';
    note.className='note'+(n?' bad':'');
    if(n)d.open=true;
  });
  return ok;
}

function markDirty(){dirty=true;$('bar').classList.add('on');
  $('bar_hint').textContent='Zmiany nie sa zapisane'}

// --- status ---------------------------------------------------------------
function cell(k,v){return '<div><div class="k">'+k+'</div><div class="v">'+v+'</div></div>'}

async function loadStatus(){
  try{
    const s=await api('/api/status');
    $('vname').textContent=s.vehicle_name?('— '+s.vehicle_name):'';
    const net=$('p_net');
    net.textContent=s.ap?('AP: '+s.ap_ssid):(s.sta?('WiFi '+s.rssi+' dBm'):'offline');
    net.className='pill '+(s.ap?'warn':(s.sta?'ok':'bad'));
    const m=$('p_mqtt');m.textContent='MQTT '+(s.mqtt?'ok':'brak');
    m.className='pill '+(s.mqtt?'ok':'bad');
    const g=$('p_gps');g.textContent=s.fix?('GPS '+s.sat+' sat'):'GPS brak fixa';
    g.className='pill '+(s.fix?'ok':'warn');

    let h='';
    h+=cell('Tryb',s.mode);
    h+=cell('Pozycja',s.fix&&s.lat!==undefined?
      (s.lat.toFixed(5)+', '+s.lon.toFixed(5)):'brak');
    h+=cell('Satelity / HDOP',s.sat+(s.hdop!==undefined?(' / '+s.hdop):''));
    h+=cell('Napiecie',s.vbat!==undefined?(s.vbat.toFixed(2)+' V'):'brak pomiaru');
    h+=cell('Kolejka offline',s.queued);
    h+=cell('Adres',s.ip);
    h+=cell('Firmware',s.fw);
    h+=cell('Czas pracy',Math.floor(s.uptime/60)+' min');
    $('status').innerHTML=h;
    $('last').textContent='odswiezono '+new Date().toLocaleTimeString('pl-PL');
  }catch(e){$('p_net').textContent='brak polaczenia';$('p_net').className='pill bad'}
}

async function loadSettings(){
  const s=await api('/api/settings');
  for(const k in s){
    const el=$(k);if(!el)continue;
    if(el.type==='checkbox')el.checked=!!s[k];else el.value=s[k];
  }
  $('ca_state').textContent=s.ca_present?'certyfikat zapisany':'brak certyfikatu';
  ['wifi_pass','mqtt_pass','apn_pass','sim_pin','ap_pass','admin_pass'].forEach(id=>{
    const el=$(id);if(!el)return;
    el.value='';
    el.placeholder=s[id+'_set']?'ustawione, puste = bez zmiany':'nieustawione';
  });
  dirty=false;$('bar').classList.remove('on');
  validateAll();
}

function collect(){
  const out={};
  document.querySelectorAll('#admin input,#admin textarea').forEach(el=>{
    if(!el.id||el.id==='auth'||el.id==='ca')return;
    const v=fieldValue(el);
    if(v===null||v==='')return;   // empty means "leave alone"
    out[el.id]=v;
  });
  // Checkboxes must always go, otherwise unchecking one would never reach the
  // device: an unchecked box is a legitimate false, not an empty field.
  document.querySelectorAll('#admin input[type=checkbox]').forEach(el=>{
    if(el.id)out[el.id]=el.checked});
  return out;
}

const authHeaders=()=>({'Content-Type':'application/json','X-Admin-Pass':$('auth').value});

// --- wiring ---------------------------------------------------------------
$('toggle').onclick=async()=>{
  adminOn=!adminOn;
  $('admin').style.display=adminOn?'block':'none';
  $('toggle').textContent=adminOn?'Zamknij tryb admin':'Tryb admin';
  $('bar').classList.toggle('on',adminOn&&dirty);
  if(adminOn){
    try{await loadSettings()}catch(e){msg('Nie udalo sie wczytac ustawien: '+e.message,false)}
    const open=JSON.parse(localStorage.getItem('ct_open')||'["s_vehicle"]');
    document.querySelectorAll('#admin details').forEach(d=>d.open=open.includes(d.id));
  }
};
$('refresh').onclick=loadStatus;

document.addEventListener('input',e=>{
  if(!e.target.id||!e.target.closest('#admin'))return;
  if(e.target.id==='auth')return;
  validateField(e.target);markDirty();
});
document.addEventListener('change',e=>{
  if(e.target.type==='checkbox'&&e.target.closest('#admin'))markDirty();
});
document.addEventListener('toggle',e=>{
  if(e.target.tagName!=='DETAILS')return;
  const open=[...document.querySelectorAll('#admin details')].filter(d=>d.open).map(d=>d.id);
  localStorage.setItem('ct_open',JSON.stringify(open));
},true);

$('save').onclick=async()=>{
  if(!validateAll()){msg('Popraw zaznaczone pola. Sekcje z bledami sa rozwiniete.',false);return}
  if(!$('auth').value){msg('Wpisz haslo administratora w pasku na dole.',false);return}
  const btn=$('save');btn.disabled=true;
  try{
    await api('/api/settings',{method:'POST',headers:authHeaders(),
      body:JSON.stringify(collect())});
    msg('Zapisane. Zmiany pinow, sieci, brokera i certyfikatu dzialaja po restarcie.',true);
    await loadSettings();
  }catch(e){msg('Odrzucone przez urzadzenie: '+e.message,false)}
  finally{btn.disabled=false}
};

$('save_ca').onclick=async()=>{
  const pem=$('ca').value.trim();
  const box=$('ca').parentElement.querySelector('.err-msg');
  if(!pem){$('ca').classList.add('err');box.textContent=
    'Pole jest puste. Do usuniecia sluzy przycisk obok';return}
  if(!pem.includes('BEGIN CERTIFICATE')||!pem.includes('END CERTIFICATE')){
    $('ca').classList.add('err');
    box.textContent='To nie wyglada na certyfikat PEM (brak linii BEGIN/END CERTIFICATE)';
    return}
  $('ca').classList.remove('err');
  try{await api('/api/ca',{method:'POST',headers:authHeaders(),body:pem});
    msg('Certyfikat zapisany.',true);$('ca').value='';await loadSettings();
  }catch(e){msg('Odrzucone: '+e.message,false)}
};
$('del_ca').onclick=async()=>{
  if(!confirm('Usunac zapisany certyfikat CA? Bez niego weryfikacja brokera przestanie dzialac.'))return;
  try{await api('/api/action',{method:'POST',headers:authHeaders(),
      body:JSON.stringify({action:'delete_ca'})});
    msg('Certyfikat usuniety.',true);await loadSettings();
  }catch(e){msg('Odrzucone: '+e.message,false)}
};

$('scan').onclick=async()=>{
  $('scan_out').textContent='skanowanie...';
  try{
    const r=await api('/api/scan');
    const dl=$('ssids');dl.innerHTML='';
    r.networks.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{
      const o=document.createElement('option');o.value=n.ssid;
      o.label=n.rssi+' dBm'+(n.open?' (otwarta)':'');dl.appendChild(o)});
    $('scan_out').textContent='znaleziono '+r.networks.length+', lista jest w polu SSID';
  }catch(e){$('scan_out').textContent='skan nieudany: '+e.message}
};

async function action(name,confirmText,okText){
  if(confirmText&&!confirm(confirmText))return;
  if(!$('auth').value){msg('Wpisz haslo administratora w pasku na dole.',false);
    $('bar').classList.add('on');return}
  try{await api('/api/action',{method:'POST',headers:authHeaders(),
      body:JSON.stringify({action:name})});
    msg(okText||('Wykonane: '+name),true);
  }catch(e){msg('Odrzucone: '+e.message,false)}
}
$('reboot').onclick=()=>action('reboot','Zrestartowac urzadzenie?',
  'Restart w toku, strona wroci za kilkanascie sekund.');
$('factory').onclick=()=>action('factory_reset',
  'Skasowac WSZYSTKIE ustawienia i wrocic do fabrycznych? Tego nie da sie cofnac.',
  'Przywrocono ustawienia fabryczne, urzadzenie sie restartuje.');
$('ap_now').onclick=()=>action('start_ap',null,'AP podniesiony.');

window.addEventListener('beforeunload',e=>{if(dirty&&adminOn){e.preventDefault();e.returnValue=''}});

loadStatus();
setInterval(loadStatus,5000);
</script>
</body></html>
)HTML";
