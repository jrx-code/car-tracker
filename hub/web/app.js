// Tracker hub UI. Built for more than one vehicle from the start: the overview
// shows every tracker at once, and the detail view is a filter on top of it,
// never a separate page.
//
// The overview is a list of rows, one car per row, with a thumbnail map on the
// right of each. The full map lives in an overlay behind "Powiększ": on a fleet
// that is parked most of the time, the map is the least changing thing on the
// page and does not deserve most of the window.

const REFRESH_MS = 10000;
const COLORS = ["#6cb6ff", "#5ad18b", "#e3b341", "#c792ea", "#ff9e64", "#7ee787"];
const TILES = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";
const MINI_ZOOM = 14;

const state = {
  vehicles: [],
  selected: null,     // vehicle_id or null for the overview
  colors: {},         // vehicle_id -> colour, stable across refreshes
  rows: {},           // vehicle_id -> row DOM and its own mini map
  markers: {},        // vehicle_id -> marker on the big map
  track: null,        // currently drawn polyline on the big map
  selectedTrip: null,
  mapFor: null,       // vehicle the big map was last opened for
};

let map;  // the big map, created on first use: it has no size until shown

function color(vehicleId) {
  if (!state.colors[vehicleId]) {
    const used = Object.keys(state.colors).length;
    state.colors[vehicleId] = COLORS[used % COLORS.length];
  }
  return state.colors[vehicleId];
}

async function api(path, options) {
  const res = await fetch(path, options);
  if (!res.ok) throw new Error(`${res.status} ${await res.text()}`);
  return res.json();
}

// --- formatting -------------------------------------------------------------

function ago(seconds) {
  if (seconds === null || seconds === undefined) return "nigdy";
  if (seconds < 60) return `${Math.round(seconds)} s temu`;
  if (seconds < 3600) return `${Math.round(seconds / 60)} min temu`;
  if (seconds < 86400) return `${(seconds / 3600).toFixed(1)} h temu`;
  return `${(seconds / 86400).toFixed(1)} dni temu`;
}

function when(ts) {
  if (!ts) return "-";
  return new Date(ts * 1000).toLocaleString("pl-PL", {
    day: "2-digit", month: "2-digit", hour: "2-digit", minute: "2-digit",
  });
}

function duration(seconds) {
  if (!seconds || seconds < 0) return "-";
  const h = Math.floor(seconds / 3600);
  const m = Math.round((seconds % 3600) / 60);
  return h ? `${h} h ${m} min` : `${m} min`;
}

const MODE_LABEL = {
  parked: "postój", driving: "jazda", moved: "RUCH NA POSTOJU", hibernate: "hibernacja",
};

// A Polish registration is written with a space after the area code, which is
// two or three characters. Devices are configured with or without it.
function plateText(plate) {
  if (!plate) return "";
  const raw = plate.replace(/\s+/g, "").toUpperCase();
  const m = raw.match(/^([A-Z]{2,3})(.+)$/);
  return m ? `${m[1]} ${m[2]}` : raw;
}

function dotIcon(fill, dimmed) {
  return L.divIcon({
    className: "",
    html: `<div style="width:16px;height:16px;border-radius:50%;background:${fill};
           border:2px solid #12141a;opacity:${dimmed ? 0.45 : 1};
           box-shadow:0 0 0 2px ${fill}55"></div>`,
    iconSize: [16, 16],
    iconAnchor: [8, 8],
  });
}

function tileLayer() {
  return L.tileLayer(TILES, { maxZoom: 19, attribution: "&copy; OpenStreetMap" });
}

// --- the fleet rows ---------------------------------------------------------

const METRICS = [
  ["Napięcie", (v, p) => (v.voltage != null ? `${v.voltage.toFixed(2)} V` : "-")],
  ["Prędkość", (v, p) => (p && p.speed != null ? `${p.speed.toFixed(0)} km/h` : "-")],
  ["Satelity / HDOP", (v, p) => (p
    ? `${p.sat ?? "-"} / ${p.hdop != null ? p.hdop.toFixed(1) : "-"}` : "-")],
  ["Sygnał", (v) => `${v.rssi != null ? `${v.rssi} dBm` : "-"} ${v.network || ""}`.trim()],
  ["Ostatni sygnał", (v) => ago(v.age_s)],
  ["Pozycja", (v, p) => (p ? `${p.lat.toFixed(5)}, ${p.lon.toFixed(5)}` : "brak fixa")],
];

function buildRow(vehicleId) {
  const el = document.createElement("article");
  el.className = "veh";
  el.dataset.vehicle = vehicleId;
  el.innerHTML = `
    <div class="veh-info">
      <div class="veh-head">
        <span class="veh-dot">&#9679;</span>
        <span class="veh-name"></span>
        <span class="plate"></span>
        <span class="veh-mode"></span>
      </div>
      <div class="vin"></div>
      <div class="grid">${METRICS.map(([k]) =>
        `<div><span class="k">${k}</span><span class="v">-</span></div>`).join("")}</div>
      <div class="warnrow"></div>
      <div class="veh-actions">
        <button data-act="detail" type="button">Szczegóły</button>
        <button data-act="portal" class="admin-only" type="button">Panel urządzenia</button>
        <span class="tile-note admin-only" hidden>panel niedostępny (brak adresu)</span>
      </div>
    </div>
    <div class="veh-map">
      <div class="mini"></div>
      <button data-act="zoom" type="button">Powiększ</button>
    </div>`;

  const row = {
    el,
    name: el.querySelector(".veh-name"),
    dot: el.querySelector(".veh-dot"),
    plate: el.querySelector(".plate"),
    vin: el.querySelector(".vin"),
    mode: el.querySelector(".veh-mode"),
    values: [...el.querySelectorAll(".grid .v")],
    warn: el.querySelector(".warnrow"),
    portalBtn: el.querySelector('[data-act="portal"]'),
    portalNote: el.querySelector(".tile-note"),
    zoomBtn: el.querySelector('[data-act="zoom"]'),
    mini: el.querySelector(".mini"),
    map: null,
    marker: null,
  };

  el.querySelector(".veh-info").addEventListener("click", () => select(vehicleId));
  el.querySelector('[data-act="detail"]').addEventListener("click", (e) => {
    e.stopPropagation();
    select(vehicleId);
  });
  row.portalBtn.addEventListener("click", (e) => {
    e.stopPropagation();
    openPortal(vehicleId);
  });
  row.zoomBtn.addEventListener("click", (e) => {
    e.stopPropagation();
    openMap(vehicleId);
  });
  return row;
}

// The thumbnail is a map, not a picture: same tiles, same marker, just small
// and inert. It is created once and then only moved, because rebuilding it on
// every ten second refresh would re-request tiles and flicker.
function updateMini(row, v) {
  const pos = v.position;
  if (!pos) {
    if (row.map) { row.map.remove(); row.map = null; row.marker = null; }
    row.mini.classList.add("nofix");
    row.mini.textContent = "brak pozycji";
    return;
  }
  row.mini.classList.remove("nofix");
  const latlng = [pos.lat, pos.lon];
  if (!row.map) {
    row.mini.textContent = "";
    row.map = L.map(row.mini, {
      zoomControl: false, attributionControl: false, dragging: false,
      scrollWheelZoom: false, doubleClickZoom: false, boxZoom: false,
      keyboard: false, touchZoom: false, tap: false,
    }).setView(latlng, MINI_ZOOM);
    tileLayer().addTo(row.map);
    row.marker = L.marker(latlng, { icon: dotIcon(color(v.vehicle_id), false) })
      .addTo(row.map);
    // Leaflet caches the container size at creation. Inside a flex row that
    // measurement lands before layout settles, and the row then changes height
    // whenever a warning line appears or goes away.
    setTimeout(() => row.map && row.map.invalidateSize(), 0);
    if (window.ResizeObserver) {
      new ResizeObserver(() => row.map && row.map.invalidateSize())
        .observe(row.mini);
    }
  } else {
    row.map.setView(latlng, MINI_ZOOM, { animate: false });
    row.marker.setLatLng(latlng);
  }
  row.marker.setIcon(dotIcon(color(v.vehicle_id), !v.online || v.stale));
}

function warningFor(v) {
  if (v.mode === "moved") return ["badrow", "Ruch przy wyłączonym silniku"];
  if (!v.online) return ["warnrow", "Offline (LWT brokera)"];
  if (v.stale) return ["warnrow", "Milczy dłużej niż zwykle"];
  // The backlog depth is the earliest warning that the link is degrading.
  if (v.queued > 0) return ["warnrow", `${v.queued} punktów w kolejce offline`];
  if (v.voltage != null && v.voltage < 12.2) return ["warnrow", "Niskie napięcie akumulatora"];
  return ["warnrow", ""];
}

function updateRow(row, v) {
  const pos = v.position;
  row.el.className = `veh ${v.mode || "parked"}${v.online ? "" : " offline"}`;
  row.dot.style.color = color(v.vehicle_id);
  row.name.textContent = v.name || v.vehicle_id;
  row.plate.textContent = plateText(v.plate);
  row.vin.textContent = v.vin ? `VIN ${v.vin}` : "";
  row.mode.textContent = MODE_LABEL[v.mode] || v.mode || "-";
  METRICS.forEach(([, fn], i) => { row.values[i].textContent = fn(v, pos); });

  const [cls, text] = warningFor(v);
  row.warn.className = cls;
  row.warn.textContent = text;

  // The device serves its own configuration portal. Offering it from the row
  // keeps one address per fleet instead of one per car, and the button only
  // appears when the device has actually reported an address we can reach.
  row.portalBtn.hidden = !v.portal;
  row.portalNote.hidden = !!v.portal;
  row.zoomBtn.disabled = !pos;

  updateMini(row, v);
}

function renderOverview() {
  document.getElementById("detail").hidden = true;
  const box = document.getElementById("vehicles");
  box.hidden = false;

  if (!state.vehicles.length) {
    for (const id of Object.keys(state.rows)) dropRow(id);
    box.innerHTML = `<div class="empty">Żaden tracker jeszcze się nie odezwał.
      Sprawdź, czy urządzenie publikuje na temat <code>cartracker/&lt;id&gt;/…</code>.</div>`;
    return;
  }
  const empty = box.querySelector(".empty");
  if (empty) empty.remove();

  const seen = new Set();
  for (const v of state.vehicles) {
    seen.add(v.vehicle_id);
    let row = state.rows[v.vehicle_id];
    if (!row) {
      row = state.rows[v.vehicle_id] = buildRow(v.vehicle_id);
      box.appendChild(row.el);
    }
    updateRow(row, v);
  }
  for (const id of Object.keys(state.rows)) if (!seen.has(id)) dropRow(id);
}

function dropRow(id) {
  const row = state.rows[id];
  if (!row) return;
  if (row.map) row.map.remove();
  row.el.remove();
  delete state.rows[id];
}

// --- the big map ------------------------------------------------------------

function ensureMap() {
  if (map) return map;
  map = L.map("map", { zoomControl: true }).setView([53.428, 14.553], 12);
  tileLayer().addTo(map);
  return map;
}

function drawVehicles() {
  if (!map) return;
  for (const v of state.vehicles) {
    const pos = v.position;
    if (!pos) continue;
    const latlng = [pos.lat, pos.lon];
    const dimmed = !v.online || v.stale;
    if (state.markers[v.vehicle_id]) {
      state.markers[v.vehicle_id].setLatLng(latlng)
        .setIcon(dotIcon(color(v.vehicle_id), dimmed));
    } else {
      state.markers[v.vehicle_id] = L.marker(latlng, {
        icon: dotIcon(color(v.vehicle_id), dimmed),
      }).addTo(map).on("click", () => { closeMap(); select(v.vehicle_id); });
    }
    state.markers[v.vehicle_id].bindPopup(
      `<b>${v.name || v.vehicle_id}</b>` +
      (v.plate ? `<br>${plateText(v.plate)}` : "") +
      `<br>${MODE_LABEL[v.mode] || v.mode || "-"}` +
      `<br>${pos.speed ? pos.speed.toFixed(0) : 0} km/h` +
      `<br>${ago(v.age_s)}`
    );
  }
}

function clearTrack() {
  if (state.track && map) {
    map.removeLayer(state.track);
  }
  state.track = null;
}

async function drawTrack(vehicleId, tripId) {
  ensureMap();
  clearTrack();
  const path = tripId
    ? `/api/vehicles/${vehicleId}/positions?trip=${tripId}`
    : `/api/vehicles/${vehicleId}/positions?hours=24`;
  const data = await api(path);
  const points = data.positions.map((p) => [p.lat, p.lon]);
  const note = document.getElementById("mapview-note");
  if (points.length < 2) {
    note.textContent = tripId ? "przejazd bez punktów" : "brak śladu z ostatnich 24 h";
    const v = state.vehicles.find((x) => x.vehicle_id === vehicleId);
    if (v && v.position) map.setView([v.position.lat, v.position.lon], 15);
    return;
  }
  note.textContent = `${points.length} punktów`;
  state.track = L.polyline(points, {
    color: color(vehicleId), weight: 4, opacity: 0.85,
  }).addTo(map);
  map.fitBounds(state.track.getBounds(), { padding: [40, 40] });
}

// Shown over the fleet list rather than beside it, so the list keeps the full
// width and the map gets the full window when it is actually being read.
function openMap(vehicleId, tripId) {
  const v = state.vehicles.find((x) => x.vehicle_id === vehicleId);
  const name = v ? (v.name || vehicleId) : vehicleId;
  const plate = v && v.plate ? ` · ${plateText(v.plate)}` : "";
  document.getElementById("mapview-title").textContent = `${name}${plate}`;
  document.getElementById("mapview-note").textContent = "";
  document.getElementById("mapview").hidden = false;
  state.mapFor = vehicleId;

  ensureMap();
  // The container had no size while the overlay was hidden, so Leaflet's cached
  // dimensions are stale every time it is reopened.
  map.invalidateSize();
  drawVehicles();
  drawTrack(vehicleId, tripId).catch((e) => {
    document.getElementById("mapview-note").textContent = `błąd: ${e.message}`;
  });
}

function closeMap() {
  document.getElementById("mapview").hidden = true;
  state.mapFor = null;
}

// --- rendering --------------------------------------------------------------

function tripRow(t) {
  const dur = t.ended ? t.ended - t.started : null;
  return `<div class="row" data-trip="${t.id}">
    <span class="when">${when(t.started)}</span>
    <span class="what">${t.open ? "w trakcie" : duration(dur)}
      &middot; maks. ${t.max_speed ? t.max_speed.toFixed(0) : 0} km/h</span>
    <span class="num">${t.distance_km.toFixed(1)} km</span>
  </div>`;
}

function eventRow(e) {
  return `<div class="row">
    <span class="when">${when(e.ts)}</span>
    <span class="what">${e.event}</span>
  </div>`;
}

// Battery voltage as an inline sparkline: for the seasonal car this is the
// single most useful view in the whole UI.
function sparkline(points) {
  if (points.length < 2) return `<div class="empty">Za mało danych.</div>`;
  const values = points.map((p) => p.voltage).filter((v) => v != null);
  if (values.length < 2) return `<div class="empty">Brak pomiarów napięcia.</div>`;
  const min = Math.min(...values), max = Math.max(...values);
  const span = Math.max(0.2, max - min);
  const w = 340, h = 72;
  const coords = points
    .filter((p) => p.voltage != null)
    .map((p, i, arr) => {
      const x = (i / (arr.length - 1)) * w;
      const y = h - ((p.voltage - min) / span) * (h - 8) - 4;
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    });
  return `<svg class="spark" viewBox="0 0 ${w} ${h}" preserveAspectRatio="none">
      <polyline fill="none" stroke="#6cb6ff" stroke-width="2" points="${coords.join(" ")}"/>
    </svg>
    <div class="grid"><div><span class="k">Zakres</span>
    <span class="v">${min.toFixed(2)} - ${max.toFixed(2)} V</span></div>
    <div><span class="k">Pomiarów</span><span class="v">${values.length}</span></div></div>`;
}

async function renderDetail(vehicleId) {
  const v = await api(`/api/vehicles/${vehicleId}`);
  document.getElementById("vehicles").hidden = true;
  const detail = document.getElementById("detail");
  detail.hidden = false;
  document.getElementById("detail-name").textContent = v.name || v.vehicle_id;
  document.getElementById("detail-plate").textContent = plateText(v.plate);
  document.getElementById("detail-vin").textContent = v.vin ? `VIN ${v.vin}` : "";

  const trips = v.trips || [];
  document.getElementById("tab-trips").innerHTML = trips.length
    ? trips.map(tripRow).join("")
    : `<div class="empty">Brak przejazdów.</div>`;
  document.getElementById("tab-trips").querySelectorAll(".row").forEach((el) => {
    el.addEventListener("click", () => {
      document.querySelectorAll("#tab-trips .row").forEach((r) => r.classList.remove("selected"));
      el.classList.add("selected");
      state.selectedTrip = el.dataset.trip;
      openMap(vehicleId, el.dataset.trip);
    });
  });

  document.getElementById("tab-events").innerHTML = (v.events || []).length
    ? v.events.map(eventRow).join("")
    : `<div class="empty">Brak zdarzeń.</div>`;

  const tel = await api(`/api/vehicles/${vehicleId}/telemetry?hours=336`);
  document.getElementById("tab-battery").innerHTML = sparkline(tel.telemetry);
}

function select(vehicleId) {
  state.selected = vehicleId;
  state.selectedTrip = null;
  document.querySelector('.tabs button[data-tab="trips"]').click();
  renderDetail(vehicleId).catch((e) => {
    document.getElementById("tab-trips").innerHTML =
      `<div class="empty">Błąd: ${e.message}</div>`;
  });
}

function deselect() {
  state.selected = null;
  state.selectedTrip = null;
  clearTrack();
  renderOverview();
}

// --- device portal ----------------------------------------------------------

// Shown in a panel over the page rather than a new tab: the point of proxying
// it is that one page covers the whole fleet, and a new tab per car undoes that.
function openPortal(vehicleId) {
  const v = state.vehicles.find((x) => x.vehicle_id === vehicleId);
  const name = v ? (v.name || vehicleId) : vehicleId;
  document.getElementById("portal-title").textContent = `Panel urządzenia — ${name}`;
  const frame = document.getElementById("portal-frame");
  frame.src = `/device/${vehicleId}/`;
  document.getElementById("portal-open").href = `/device/${vehicleId}/`;
  document.getElementById("portal").hidden = false;
}

function closePortal() {
  document.getElementById("portal").hidden = true;
  document.getElementById("portal-frame").src = "about:blank";
}

// --- polling ----------------------------------------------------------------

async function refresh() {
  try {
    const [vehicles, health] = await Promise.all([
      api("/api/vehicles"),
      api("/api/health"),
    ]);
    state.vehicles = vehicles.vehicles;
    const pill = document.getElementById("health");
    pill.textContent = health.mqtt.connected
      ? `MQTT ok · ${state.vehicles.length} trackerów · ${health.db.positions} pozycji`
      : "MQTT rozłączony";
    pill.className = "pill " + (health.mqtt.connected ? "ok" : "bad");

    if (!state.selected) renderOverview();
    drawVehicles();
  } catch (e) {
    const pill = document.getElementById("health");
    pill.textContent = "brak połączenia z hubem";
    pill.className = "pill bad";
  }
}

function initUi() {
  document.getElementById("back").addEventListener("click", deselect);
  document.getElementById("portal-close").addEventListener("click", closePortal);
  document.getElementById("mapview-close").addEventListener("click", closeMap);
  document.addEventListener("keydown", (e) => {
    if (e.key !== "Escape") return;
    if (!document.getElementById("portal").hidden) closePortal();
    else closeMap();
  });

  document.querySelectorAll(".tabs button").forEach((btn) => {
    btn.addEventListener("click", () => {
      document.querySelectorAll(".tabs button").forEach((b) => b.classList.remove("active"));
      btn.classList.add("active");
      for (const name of ["trips", "events", "battery", "settings"]) {
        document.getElementById(`tab-${name}`).hidden = name !== btn.dataset.tab;
      }
      if (btn.dataset.tab === "settings" && state.selected) {
        renderSettings(state.selected);  // admin.js
      }
    });
  });

  document.querySelectorAll(".commands button").forEach((btn) => {
    btn.addEventListener("click", async () => {
      if (!state.selected) return;
      const out = document.getElementById("cmd-result");
      out.textContent = "wysyłanie…";
      try {
        const res = await api(`/api/admin/vehicles/${state.selected}/command`, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ cmd: btn.dataset.cmd }),
        });
        out.textContent = res.sent ? "wysłane" : "nie wysłano";
      } catch (e) {
        out.textContent = `błąd: ${e.message}`;
      }
    });
  });
}

initUi();
refresh();
setInterval(refresh, REFRESH_MS);
