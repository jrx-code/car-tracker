// Admin mode. Signing in is Authentik's job: the button navigates to /admin,
// gateway-caddy sends the browser through the login flow, and the hub answers
// with a redirect to /#admin. From then on the page may call /api/admin.
//
// Hiding the controls is convenience, not security. The hub refuses every
// write that does not come from a signed-in admin, whatever this page shows.

const ADMIN_KEY = "tracker-admin";

// Device portal settings, grouped the way a person thinks about the car rather
// than the order of the struct in firmware. Keys are the portal's own
// (car-tracker docs/13). A password field is write only: the portal reports
// "<key>_set" and an empty input means "leave it as it is".
const SETTINGS = [
  {
    title: "Karta SIM i sieć komórkowa",
    fields: [
      ["modem_enabled", "bool", "Modem włączony"],
      ["apn", "text", "APN"],
      ["apn_user", "text", "Użytkownik APN"],
      ["apn_pass", "password", "Hasło APN"],
      ["sim_pin", "password", "PIN karty SIM", "zalecane puste: PIN wyłączony na karcie"],
      ["allow_roaming", "bool", "Roaming dozwolony"],
    ],
  },
  {
    title: "Pojazd",
    fields: [
      ["vehicle_id", "readonly", "Identyfikator (temat MQTT)"],
      ["vehicle_name", "text", "Nazwa"],
      ["plate", "text", "Numer rejestracyjny"],
      ["vin", "text", "VIN", "17 znaków albo puste"],
    ],
  },
  {
    title: "WiFi",
    fields: [
      ["wifi_enabled", "bool", "WiFi włączone"],
      ["wifi_ssid", "text", "Sieć (SSID)"],
      ["wifi_pass", "password", "Hasło WiFi"],
      ["wifi_timeout_s", "number", "Limit łączenia [s]"],
    ],
  },
  {
    title: "MQTT",
    fields: [
      ["mqtt_host", "text", "Broker"],
      ["mqtt_port", "number", "Port"],
      ["mqtt_user", "text", "Użytkownik"],
      ["mqtt_pass", "password", "Hasło"],
      ["mqtt_tls", "bool", "TLS"],
      ["mqtt_verify_ca", "bool", "Weryfikacja certyfikatu"],
      ["mqtt_keepalive", "number", "Keepalive [s]"],
      ["topic_prefix", "text", "Prefiks tematów"],
      ["ca_present", "readonly", "Własny certyfikat CA wgrany"],
    ],
  },
  {
    title: "Portal i punkt dostępowy",
    fields: [
      ["portal_enabled", "bool", "Portal włączony"],
      ["hostname", "text", "Nazwa hosta"],
      ["ap_enabled", "bool", "Awaryjny punkt dostępowy"],
      ["ap_ssid", "text", "SSID punktu (puste = domyślne)"],
      ["ap_ssid_effective", "readonly", "SSID w użyciu"],
      ["ap_pass", "password", "Hasło punktu"],
      ["ap_after_s", "number", "Włącz po [s] bez sieci"],
      ["ap_timeout_s", "number", "Wyłącz po [s]"],
      ["ota_enabled", "bool", "Aktualizacja OTA"],
      ["admin_pass", "password", "Hasło administratora portalu",
        "po zmianie ustaw to samo w DEVICE_ADMIN_PASS huba"],
    ],
  },
  {
    title: "Piny (zaawansowane)",
    collapsed: true,
    fields: [
      ["pin_gnss_rx", "number", "GNSS RX (-1: GNSS w modemie)"],
      ["pin_gnss_tx", "number", "GNSS TX"],
      ["pin_gnss_en", "number", "GNSS zasilanie"],
      ["gnss_baud", "number", "GNSS prędkość UART"],
      ["pin_modem_rx", "number", "Modem RX"],
      ["pin_modem_tx", "number", "Modem TX"],
      ["pin_modem_pwrkey", "number", "Modem PWRKEY"],
      ["pin_modem_en", "number", "Modem zasilanie"],
      ["pin_vbat_adc", "number", "Pomiar napięcia (-1: brak)"],
      ["pin_acc_int", "number", "Akcelerometr INT"],
      ["pin_i2c_sda", "number", "I2C SDA"],
      ["pin_i2c_scl", "number", "I2C SCL"],
      ["pin_led", "number", "LED (-1: brak)"],
    ],
  },
];

// Operating parameters travel as the retained cfg topic over MQTT, so unlike
// the portal settings above they reach a car that is out on LTE.
const CFG = [
  ["int_drive", "Interwał w jeździe [s]"],
  ["int_park", "Interwał na postoju [s]"],
  ["int_alarm", "Interwał alarmowy [s]"],
  ["v_drive_on", "Silnik pracuje od [V]"],
  ["v_drive_off", "Silnik stoi poniżej [V]"],
  ["v_warn", "Ostrzeżenie o akumulatorze [V]"],
  ["v_hib", "Hibernacja poniżej [V] (min. 11,0)"],
  ["v_wake", "Wybudzenie od [V]"],
  ["crs_delta", "Punkt przy zmianie kursu o [°]"],
  ["hdop_max", "Maksymalny HDOP"],
  ["motion_sens", "Czułość ruchu (1-5)"],
];

state.admin = null;

function esc(value) {
  return String(value ?? "").replace(/[&<>"']/g, (c) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
  }[c]));
}

function applyAdmin() {
  document.body.classList.toggle("admin", !!state.admin);
  document.getElementById("admin-user").textContent =
    state.admin ? `administrator: ${state.admin}` : "";
  const btn = document.getElementById("admin-btn");
  btn.textContent = state.admin ? "Zakończ tryb administratora" : "Tryb administratora";
  btn.classList.toggle("active", !!state.admin);
  // Leaving admin mode while the settings tab is open would leave it on screen.
  if (!state.admin && !document.getElementById("tab-settings").hidden) {
    document.querySelector('.tabs button[data-tab="trips"]').click();
  }
}

async function initAdmin() {
  let wanted = location.hash === "#admin";
  try { wanted = wanted || sessionStorage.getItem(ADMIN_KEY) === "1"; } catch (e) { /* private mode */ }
  if (location.hash === "#admin") history.replaceState(null, "", location.pathname);
  if (!wanted) { applyAdmin(); return; }
  try {
    // redirect: "manual" because an expired Authentik session answers with a
    // redirect to the login page, which fetch must not follow cross-origin.
    const res = await fetch("/api/admin/whoami", { redirect: "manual" });
    if (res.ok) {
      state.admin = (await res.json()).user;
      try { sessionStorage.setItem(ADMIN_KEY, "1"); } catch (e) { /* ignore */ }
    }
  } catch (e) { /* not signed in */ }
  if (!state.admin) {
    try { sessionStorage.removeItem(ADMIN_KEY); } catch (e) { /* ignore */ }
  }
  applyAdmin();
}

function toggleAdmin() {
  if (state.admin) {
    state.admin = null;
    try { sessionStorage.removeItem(ADMIN_KEY); } catch (e) { /* ignore */ }
    applyAdmin();
    return;
  }
  location.href = "/admin";
}

// --- settings tab -------------------------------------------------------------

function fieldHtml(key, type, label, hint, settings) {
  const id = `set-${key}`;
  if (type === "readonly") {
    const raw = settings[key];
    const shown = raw === true ? "tak" : raw === false ? "nie" : (raw ?? "-");
    return `<label class="f"><span>${esc(label)}</span>
      <output>${esc(shown)}</output></label>`;
  }
  if (type === "bool") {
    return `<label class="f fb"><input type="checkbox" id="${id}" data-key="${key}"
      data-type="bool" ${settings[key] ? "checked" : ""}><span>${esc(label)}</span></label>`;
  }
  if (type === "password") {
    const set = settings[`${key}_set`];
    const ph = set ? "ustawione, puste = bez zmian" : "nieustawione";
    return `<label class="f"><span>${esc(label)}</span>
      <input type="password" id="${id}" data-key="${key}" data-type="password"
        autocomplete="new-password" placeholder="${ph}">
      ${hint ? `<small>${esc(hint)}</small>` : ""}</label>`;
  }
  const inputType = type === "number" ? "number" : "text";
  return `<label class="f"><span>${esc(label)}</span>
    <input type="${inputType}" id="${id}" data-key="${key}" data-type="${type}"
      value="${esc(settings[key])}">
    ${hint ? `<small>${esc(hint)}</small>` : ""}</label>`;
}

function identityHtml(data) {
  const v = data.vehicle || {};
  const info = data.info || {};
  const rows = [
    ["Modem", v.modem],
    ["IMEI / MAC", v.imei],
    ["ICCID", info.iccid],
    ["Sieć", v.network],
    ["Operator", v.operator],
    ["Sygnał", v.rssi != null ? `${v.rssi} dBm` : null],
    ["Adres portalu", v.ip],
    ["Firmware", v.firmware],
  ];
  return `<div class="grid ident">${rows.map(([k, val]) =>
    `<div><span class="k">${k}</span><span class="v">${esc(val || "-")}</span></div>`).join("")}</div>`;
}

function ackHtml(acks) {
  if (!acks.length) return `<div class="empty">Brak odpowiedzi na komendy od startu huba.</div>`;
  return acks.slice(0, 8).map((a) => `<div class="row">
    <span class="when">${when(a.received)}</span>
    <span class="what">${a.ok ? "ok" : "błąd"} ${esc(a.msg || "")}</span>
    <span class="num">${a.ms != null ? `${a.ms} ms` : ""}</span></div>`).join("");
}

async function renderSettings(vehicleId) {
  const box = document.getElementById("tab-settings");
  if (!state.admin) { box.innerHTML = ""; return; }
  box.innerHTML = `<div class="empty">wczytywanie…</div>`;
  let data;
  try {
    data = await api(`/api/admin/vehicles/${vehicleId}`);
  } catch (e) {
    box.innerHTML = `<div class="empty">Błąd: ${esc(e.message)}</div>`;
    return;
  }
  if (state.selected !== vehicleId) return;  // the user moved on meanwhile

  const s = data.settings;
  const cfg = data.cfg || {};
  let html = `<section class="adm"><h3>Łączność i tożsamość</h3>${identityHtml(data)}</section>`;

  if (!s) {
    html += `<div class="badrow adm-note">Panel urządzenia nieosiągalny: ${esc(data.settings_error)}.
      Ustawienia urządzenia da się zmienić tylko wtedy, gdy tracker jest w sieci domowej.
      Parametry pracy i komendy poniżej idą przez MQTT i działają zawsze.</div>`;
  } else {
    if (!data.device_pass_configured) {
      html += `<div class="warnrow adm-note">Hub nie ma ustawionego DEVICE_ADMIN_PASS,
        więc zapis ustawień urządzenia zostanie odrzucony.</div>`;
    }
    for (const sec of SETTINGS) {
      html += `<details class="adm" ${sec.collapsed ? "" : "open"}>
        <summary>${esc(sec.title)}</summary>
        <form class="adm-form" data-kind="settings">
          ${sec.fields.map(([k, t, l, h]) => fieldHtml(k, t, l, h, s)).join("")}
          <div class="adm-save"><button type="submit">Zapisz</button><span class="adm-out"></span></div>
        </form></details>`;
    }
  }

  html += `<details class="adm" open><summary>Parametry pracy (przez MQTT)</summary>
    <form class="adm-form" data-kind="cfg">
      <p class="adm-hint">Idą retained na temat <code>cfg</code>, więc dotrą też do auta na LTE,
      przy najbliższym połączeniu. Puste pole: wartość bez zmian.</p>
      ${CFG.map(([k, l]) => `<label class="f"><span>${esc(l)}</span>
        <input type="number" step="any" data-key="${k}" data-type="cfg"
          value="${esc(cfg[k])}" placeholder="domyślna urządzenia"></label>`).join("")}
      <div class="adm-save"><button type="submit">Wyślij</button><span class="adm-out"></span></div>
    </form></details>`;

  html += `<details class="adm" open><summary>Komendy</summary>
    <div class="adm-cmds">
      <button data-admcmd="locate" type="button">Zlokalizuj teraz</button>
      <button data-admcmd="ping" type="button">Ping</button>
      <button data-admcmd="reboot" type="button">Restart (MQTT)</button>
      ${s ? `<button data-admact="start_ap" type="button">Włącz punkt dostępowy</button>` : ""}
      <span class="adm-out" id="adm-cmd-out"></span>
    </div>
    <h4>Ostatnie odpowiedzi</h4><div id="adm-acks">${ackHtml(data.acks || [])}</div>
  </details>`;

  box.innerHTML = html;

  box.querySelectorAll("form.adm-form").forEach((form) => {
    form.addEventListener("submit", (e) => {
      e.preventDefault();
      if (form.dataset.kind === "cfg") saveCfg(vehicleId, form);
      else saveSettings(vehicleId, form, s);
    });
  });
  box.querySelectorAll("[data-admcmd]").forEach((btn) => btn.addEventListener("click", () =>
    sendCmd(vehicleId, btn.dataset.admcmd)));
  box.querySelectorAll("[data-admact]").forEach((btn) => btn.addEventListener("click", () =>
    deviceAction(vehicleId, btn.dataset.admact)));
}

async function postJson(path, body) {
  return api(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
}

// Only what changed goes to the device: the portal treats a missing key as
// "keep", and sending the whole form would rewrite values nobody touched.
async function saveSettings(vehicleId, form, original) {
  const out = form.querySelector(".adm-out");
  const body = {};
  for (const input of form.querySelectorAll("input[data-key]")) {
    const key = input.dataset.key;
    const type = input.dataset.type;
    if (type === "bool") {
      if (input.checked !== !!original[key]) body[key] = input.checked;
    } else if (type === "password") {
      if (input.value !== "") body[key] = input.value;
    } else if (type === "number") {
      if (input.value !== "" && Number(input.value) !== original[key]) {
        body[key] = Number(input.value);
      }
    } else if (input.value !== (original[key] ?? "")) {
      body[key] = input.value;
    }
  }
  if (!Object.keys(body).length) { out.textContent = "bez zmian"; return; }
  out.textContent = "zapisywanie…";
  try {
    await postJson(`/api/admin/vehicles/${vehicleId}/settings`, body);
    out.innerHTML = `zapisane (${Object.keys(body).map(esc).join(", ")}).
      Sieć, broker, TLS i piny działają po restarcie.
      <button type="button" class="adm-reboot">Restartuj teraz</button>`;
    out.querySelector(".adm-reboot").addEventListener("click", () =>
      deviceAction(vehicleId, "reboot", out));
    Object.assign(original, Object.fromEntries(
      Object.entries(body).filter(([k]) => !k.endsWith("pass") && k !== "sim_pin")));
    form.querySelectorAll('input[data-type="password"]').forEach((i) => { i.value = ""; });
  } catch (e) {
    out.textContent = `błąd: ${e.message}`;
  }
}

async function saveCfg(vehicleId, form) {
  const out = form.querySelector(".adm-out");
  const body = {};
  for (const input of form.querySelectorAll("input[data-key]")) {
    if (input.value !== "") body[input.dataset.key] = Number(input.value);
  }
  if (!Object.keys(body).length) { out.textContent = "nic do wysłania"; return; }
  out.textContent = "wysyłanie…";
  try {
    const res = await postJson(`/api/admin/vehicles/${vehicleId}/config`, body);
    out.textContent = res.sent ? "wysłane na broker (retained)" : "broker nie przyjął";
  } catch (e) {
    out.textContent = `błąd: ${e.message}`;
  }
}

async function sendCmd(vehicleId, cmd) {
  const out = document.getElementById("adm-cmd-out");
  if (cmd === "reboot" && !confirm("Zrestartować tracker?")) return;
  out.textContent = "wysyłanie…";
  try {
    const res = await postJson(`/api/admin/vehicles/${vehicleId}/command`, { cmd });
    out.textContent = res.sent
      ? (cmd === "locate" ? "wysłane, odpowiedź do 60 s" : "wysłane")
      : "nie wysłano";
    // The ack comes back over MQTT; show it once it has had time to arrive.
    setTimeout(() => refreshAcks(vehicleId), cmd === "locate" ? 20000 : 4000);
  } catch (e) {
    out.textContent = `błąd: ${e.message}`;
  }
}

async function deviceAction(vehicleId, action, outEl) {
  const out = outEl || document.getElementById("adm-cmd-out");
  if (action === "reboot" && !confirm("Zrestartować tracker?")) return;
  out.textContent = "wysyłanie…";
  try {
    await postJson(`/api/admin/vehicles/${vehicleId}/action`, { action });
    out.textContent = action === "reboot" ? "restartuje się" : "wykonane";
  } catch (e) {
    out.textContent = `błąd: ${e.message}`;
  }
}

async function refreshAcks(vehicleId) {
  if (state.selected !== vehicleId) return;
  try {
    const data = await api(`/api/admin/vehicles/${vehicleId}`);
    const box = document.getElementById("adm-acks");
    if (box) box.innerHTML = ackHtml(data.acks || []);
  } catch (e) { /* keep what is shown */ }
}

document.getElementById("admin-btn").addEventListener("click", toggleAdmin);
initAdmin();
