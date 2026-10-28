(() => {
  'use strict';

  const SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
  const CHAR_RX_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';
  const CHAR_TX_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';
  const STORAGE_KEY = 'tdongle_k400_snippets_v2';

  let bleDevice = null;
  let rxChar = null;
  let txChar = null;
  let gattQueue = Promise.resolve();
  let pendingWrites = 0;
  let lastGattOpEnd = 0;
  let lastWriteActivity = 0;
  let mouseBusy = false;
  let rxLineBuffer = '';
  let statusPollTimer = null;

  const sleep = (ms) => new Promise((res) => setTimeout(res, ms));

  function enqueueGatt(opFn) {
    const run = async () => {
      const elapsed = performance.now() - lastGattOpEnd;
      if (elapsed < 8) {
        await sleep(8 - elapsed);
      }
      try {
        return await opFn();
      } finally {
        lastGattOpEnd = performance.now();
      }
    };
    const next = gattQueue.then(run, run);
    gattQueue = next.catch(() => {});
    return next;
  }

  let deviceState = {
    j: 1,
    min: 30,
    max: 60,
    mode: 0,
    auto: 1,
    lay: 'DE',
    delay: 12,
    rnd: 0,
    rmax: 55,
    stealth: 0,
    cnt: 0,
    next: 30,
    pin: 400400
  };

  let dongleSnippets = [];
  let activeCategory = 'ALL';
  let editingSnippetId = null;

  const DEFAULT_SNIPPETS = [
    {
      id: 's1',
      title: 'Mit freundlichen Grüßen',
      cat: 'E-Mail',
      text: 'Mit freundlichen Grüßen\n',
      enter: false,
      slot: 0
    },
    {
      id: 's2',
      title: 'Bitte um Rückruf',
      cat: 'E-Mail',
      text: 'Hallo,\nbitte um einen kurzen Rückruf bei Gelegenheit. Vielen Dank!',
      enter: true,
      slot: 1
    },
    {
      id: 's3',
      title: 'Terminbestätigung',
      cat: 'Vorlagen',
      text: 'Vielen Dank für die Einladung – der vorgeschlagene Termin passt bei mir perfekt.',
      enter: true,
      slot: 2
    },
    {
      id: 's4',
      title: 'Login-Sequenz (Tab + Pause)',
      cat: 'Logins',
      text: 'benutzername{TAB}{W:400}Passwort123!',
      enter: true,
      slot: 3
    },
    {
      id: 's5',
      title: 'Git Status & Log',
      cat: 'Befehle',
      text: 'git status && git log -n 5 --oneline',
      enter: true,
      slot: -1
    }
  ];

  let snippets = loadSnippets();

  // --- DOM Helpers ---
  const $ = (id) => document.getElementById(id);

  function haptic(ms = 12) {
    if (navigator.vibrate) {
      try { navigator.vibrate(ms); } catch (_) {}
    }
  }

  let toastTimer = null;
  function showToast(msg) {
    const el = $('toastMsg');
    if (!el) return;
    el.textContent = msg;
    el.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => el.classList.remove('show'), 2100);
  }

  function addLog(dir, text) {
    const box = $('logBox');
    if (!box) return;
    const div = document.createElement('div');
    div.className = dir === 'TX' ? 'log-tx' : dir === 'ERR' ? 'log-err' : 'log-rx';
    const time = new Date().toLocaleTimeString('de-DE', { hour12: false });
    div.textContent = `[${time}] ${dir}: ${text}`;
    box.appendChild(div);
    while (box.children.length > 80) box.removeChild(box.firstChild);
    box.scrollTop = box.scrollHeight;
  }

  function insertAtCursor(textarea, token) {
    if (!textarea) return;
    const start = textarea.selectionStart ?? textarea.value.length;
    const end = textarea.selectionEnd ?? textarea.value.length;
    const before = textarea.value.slice(0, start);
    const after = textarea.value.slice(end);
    textarea.value = before + token + after;
    const newPos = start + token.length;
    textarea.focus();
    textarea.setSelectionRange(newPos, newPos);
    textarea.dispatchEvent(new Event('input'));
  }

  // --- Escape / Unescape für mehrzeilige Strings im Zeilenprotokoll ---
  function escapeForWire(str) {
    return str
      .replace(/\\/g, '\\\\')
      .replace(/\r\n/g, '\\n')
      .replace(/\n/g, '\\n')
      .replace(/\t/g, '\\t');
  }

  function unescapeFromWire(str) {
    let out = '';
    for (let i = 0; i < str.length; i++) {
      if (str[i] === '\\' && i + 1 < str.length) {
        const n = str[i + 1];
        if (n === 'n') { out += '\n'; i++; continue; }
        if (n === 't') { out += '\t'; i++; continue; }
        if (n === '\\') { out += '\\'; i++; continue; }
      }
      out += str[i];
    }
    return out;
  }

  // --- BLE Communication & Status Sync ---
  function isConnected() {
    return !!(bleDevice && bleDevice.gatt && bleDevice.gatt.connected && rxChar);
  }

  function updateConnectionUi() {
    const btn = $('btnConnect');
    const lbl = $('btnConnectLabel');
    const sub = $('connSubText');
    if (isConnected()) {
      btn.classList.add('connected');
      lbl.textContent = 'Verbunden';
      sub.textContent = `USB Receiver • PIN ${deviceState.pin}`;
    } else {
      btn.classList.remove('connected');
      lbl.textContent = 'Verbinden';
      sub.textContent = `Bereit • Standard-PIN ${deviceState.pin}`;
    }
    updateStatusPills(false);
  }

  function updateStatusPills(syncInputs = true) {
    const pJig = $('pillJiggler');
    const pJigTxt = $('pillJigglerText');
    const pLay = $('pillLayout');
    const pLayTxt = $('pillLayoutText');
    const pStl = $('pillStealth');
    const pStlTxt = $('pillStealthText');

    if (deviceState.j) {
      pJig.className = 'status-pill active-green';
      pJigTxt.textContent = `Jiggler: AN (${deviceState.next}s • #${deviceState.cnt})`;
    } else {
      pJig.className = 'status-pill';
      pJigTxt.textContent = `Jiggler: PAUSE (#${deviceState.cnt})`;
    }

    pLay.className = 'status-pill active-blue';
    pLayTxt.textContent = `Layout: ${deviceState.lay}-QWERT${deviceState.lay === 'DE' ? 'Z' : 'Y'}`;

    if (deviceState.stealth) {
      pStl.className = 'status-pill active-warn';
      pStlTxt.textContent = 'Stealth: DUNKEL';
    } else {
      pStl.className = 'status-pill';
      pStlTxt.textContent = 'Display: AN';
    }

    $('jiggleStatsBadge').textContent = `#${deviceState.cnt} Bewegungen`;

    if (syncInputs) {
      if (document.activeElement !== $('selJiggleMode')) {
        $('selJiggleMode').value = String(deviceState.mode);
      }
      if (document.activeElement !== $('inpJiggleMin')) {
        $('inpJiggleMin').value = String(deviceState.min);
      }
      if (document.activeElement !== $('inpJiggleMax')) {
        $('inpJiggleMax').value = String(deviceState.max);
      }
      $('chkJiggleAuto').checked = !!deviceState.auto;
      if (document.activeElement !== $('selLayout')) {
        $('selLayout').value = deviceState.lay;
      }
      if (document.activeElement !== $('inpKeyDelay')) {
        $('inpKeyDelay').value = String(deviceState.delay);
      }
      if (document.activeElement !== $('inpKeyDelayMax')) {
        $('inpKeyDelayMax').value = String(deviceState.rmax || 55);
      }
      $('chkRandomDelay').checked = !!deviceState.rnd;
      if (document.activeElement !== $('inpBlePin')) {
        $('inpBlePin').value = String(deviceState.pin);
      }
    }
  }

  function applyStatJson(jsonStr, logIt = false) {
    try {
      const obj = JSON.parse(jsonStr);
      const changed =
        obj.j !== deviceState.j ||
        obj.lay !== deviceState.lay ||
        obj.stealth !== deviceState.stealth ||
        obj.rnd !== deviceState.rnd;
      deviceState = { ...deviceState, ...obj };
      updateStatusPills(changed);
      if (logIt) addLog('RX', `STAT ${jsonStr}`);
    } catch (_) {}
  }

  // Liest den vollständigen Status per GATT Read von txChar über die zentrale GATT-Queue
  async function readStatusFromDongle(logIt = false, force = false) {
    if (!isConnected() || !txChar) return;
    // Während laufender/schneller Eingabefolgen kein periodisches Status-Read einstreuen
    if (!force && (pendingWrites > 0 || performance.now() - lastWriteActivity < 900)) {
      return;
    }
    return enqueueGatt(async () => {
      if (!isConnected() || !txChar) return;
      if (!force && pendingWrites > 0) return;
      try {
        const dv = await txChar.readValue();
        const text = new TextDecoder('utf-8').decode(dv.buffer).trim();
        if (text.startsWith('STAT ')) {
          applyStatJson(text.slice(5), logIt);
        }
      } catch (_) {}
    });
  }

  function onTxNotification(event) {
    const bytes = new Uint8Array(event.target.value.buffer);
    const chunk = new TextDecoder('utf-8').decode(bytes);
    rxLineBuffer += chunk;

    let nlIdx;
    while ((nlIdx = rxLineBuffer.indexOf('\n')) >= 0) {
      const line = rxLineBuffer.slice(0, nlIdx).trim();
      rxLineBuffer = rxLineBuffer.slice(nlIdx + 1);
      if (line) handleIncomingLine(line);
    }
  }

  function handleIncomingLine(line) {
    if (line.startsWith('STAT ')) {
      applyStatJson(line.slice(5), false);
      return;
    }
    addLog('RX', line);
    if (line.startsWith('SNIP ')) {
      const rest = line.slice(5);
      if (rest === 'END') {
        renderDongleSnippets();
        return;
      }
      const b1 = rest.indexOf('|');
      const b2 = rest.indexOf('|', b1 + 1);
      if (b1 > 0 && b2 > b1) {
        const slot = parseInt(rest.slice(0, b1), 10);
        const title = rest.slice(b1 + 1, b2);
        const text = unescapeFromWire(rest.slice(b2 + 1));
        dongleSnippets = dongleSnippets.filter((s) => s.slot !== slot);
        dongleSnippets.push({ slot, title, text });
        dongleSnippets.sort((a, b) => a.slot - b.slot);
      }
      return;
    }
    if (line.startsWith('ERR')) {
      showToast(`Fehler: ${line}`);
    }
  }

  async function connectBle() {
    if (!navigator.bluetooth) {
      alert('Web Bluetooth wird in diesem Browser nicht unterstützt. Bitte Chrome auf deinem Pixel 10 Pro XL über HTTPS verwenden.');
      return;
    }
    try {
      if (isConnected()) {
        clearInterval(statusPollTimer);
        bleDevice.gatt.disconnect();
        return;
      }
      showToast('Suche "USB Receiver"...');
      bleDevice = await navigator.bluetooth.requestDevice({
        filters: [
          { services: [SERVICE_UUID] },
          { name: 'USB Receiver' }
        ],
        optionalServices: [SERVICE_UUID]
      });

      bleDevice.addEventListener('gattserverdisconnected', () => {
        clearInterval(statusPollTimer);
        rxChar = null;
        txChar = null;
        updateConnectionUi();
        showToast('BLE getrennt');
      });

      const server = await bleDevice.gatt.connect();
      const service = await server.getPrimaryService(SERVICE_UUID);
      rxChar = await service.getCharacteristic(CHAR_RX_UUID);
      txChar = await service.getCharacteristic(CHAR_TX_UUID);

      await txChar.startNotifications();
      txChar.addEventListener('characteristicvaluechanged', onTxNotification);

      updateConnectionUi();
      haptic(25);
      showToast('Mit T-Dongle S3 verbunden!');

      dongleSnippets = [];
      await sendCommand('SYS STATUS');
      await readStatusFromDongle(true, true);
      updateStatusPills(true);
      await sendCommand('SNIP LIST');

      // Jede Sekunde Status vom Dongle lesen, damit Countdown & Zähler live laufen
      clearInterval(statusPollTimer);
      statusPollTimer = setInterval(() => {
        readStatusFromDongle(false, false);
      }, 1150);
    } catch (err) {
      addLog('ERR', err.message || String(err));
      showToast('Verbindung abgebrochen');
    }
  }

  async function writeSliceWithRetry(slice, preferNoResponse) {
    let lastErr = null;
    for (let attempt = 0; attempt < 4; attempt++) {
      if (!isConnected()) throw new Error('BLE getrennt');
      if (attempt > 0) {
        await sleep(22 * attempt);
      }
      try {
        if (preferNoResponse && typeof rxChar.writeValueWithoutResponse === 'function') {
          await rxChar.writeValueWithoutResponse(slice);
        } else if (typeof rxChar.writeValueWithResponse === 'function') {
          await rxChar.writeValueWithResponse(slice);
        } else {
          await rxChar.writeValue(slice);
        }
        return;
      } catch (err) {
        lastErr = err;
        // Bei kurzem Android-GATT-Stau im 3. Versuch über WriteWithoutResponse zustellen
        if (attempt >= 2 && typeof rxChar.writeValueWithoutResponse === 'function') {
          try {
            await sleep(25);
            await rxChar.writeValueWithoutResponse(slice);
            return;
          } catch (_) {}
        }
      }
    }
    throw lastErr;
  }

  function sendCommand(cmdLine, silent = false) {
    if (!isConnected()) {
      if (!silent) showToast('Nicht verbunden – bitte oben auf Verbinden tippen');
      return Promise.resolve(false);
    }

    if (!silent) addLog('TX', cmdLine);

    const payload = new TextEncoder().encode(cmdLine + '\n');
    const chunkSize = 20;
    pendingWrites++;
    lastWriteActivity = performance.now();

    return enqueueGatt(async () => {
      try {
        for (let offset = 0; offset < payload.length; offset += chunkSize) {
          if (offset > 0) {
            await sleep(6);
          }
          const slice = payload.slice(offset, offset + chunkSize);
          await writeSliceWithRetry(slice, silent);
        }
        return true;
      } catch (err) {
        const msg = err && err.message ? err.message : String(err);
        addLog('ERR', msg);
        if (!silent) showToast(`BLE-Fehler: ${msg}`);
        return false;
      } finally {
        pendingWrites = Math.max(0, pendingWrites - 1);
        lastWriteActivity = performance.now();
      }
    });
  }

  // Sendet auch sehr lange Textmodule inkl. temporärem Tipp-Modus zuverlässig
  async function typeLongText(rawText, appendEnter = false, clearBefore = false, timingOverride = 'dongle') {
    if (!isConnected()) {
      showToast('Bitte zuerst mit dem Dongle verbinden!');
      return;
    }
    haptic(15);

    const origDelay = deviceState.delay || 12;
    const origRnd = deviceState.rnd ? 1 : 0;
    const origRmax = deviceState.rmax || 55;
    let changedTiming = false;

    if (timingOverride === 'human') {
      await sendCommand('SYS DELAY 18 1 75');
      changedTiming = true;
    } else if (timingOverride === 'fast') {
      await sendCommand('SYS DELAY 8 0 15');
      changedTiming = true;
    } else if (timingOverride === 'slow') {
      await sendCommand('SYS DELAY 90 0 90');
      changedTiming = true;
    } else if (timingOverride === 'custom') {
      const customMs = Math.max(5, Math.min(500, parseInt($('inpCustomCharPause').value, 10) || 120));
      await sendCommand(`SYS DELAY ${customMs} 0 ${customMs}`);
      changedTiming = true;
    }

    if (clearBefore) {
      await sendCommand('K STRG+A');
      await sendCommand('K BACKSPACE');
    }

    const fullText = appendEnter ? rawText + '\n' : rawText;
    const maxSegmentChars = 500;
    for (let i = 0; i < fullText.length; i += maxSegmentChars) {
      const seg = fullText.slice(i, i + maxSegmentChars);
      const escaped = escapeForWire(seg);
      await sendCommand(`T ${escaped}`);
    }

    if (changedTiming) {
      await sendCommand(`SYS DELAY ${origDelay} ${origRnd} ${origRmax}`);
    }
    showToast('Text gesendet ✓');
  }

  // --- Snippets Management ---
  function loadSnippets() {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (raw) {
        const parsed = JSON.parse(raw);
        if (Array.isArray(parsed) && parsed.length > 0) return parsed;
      }
    } catch (_) {}
    return DEFAULT_SNIPPETS;
  }

  function saveSnippets() {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(snippets));
    renderSnippets();
  }

  function renderSnippets() {
    const list = $('snippetList');
    if (!list) return;
    list.innerHTML = '';

    const visible = snippets.filter(
      (s) => activeCategory === 'ALL' || s.cat === activeCategory
    );

    if (visible.length === 0) {
      list.innerHTML = '<div style="font-size:0.82rem; color:var(--text-muted);">Keine Textmodule in dieser Kategorie.</div>';
      return;
    }

    visible.forEach((s) => {
      const card = document.createElement('div');
      card.className = 'snippet-card';

      const slotBadge = s.slot >= 0 ? `<span class="snippet-cat" style="color:var(--primary)">Dongle #${s.slot}</span>` : '';
      card.innerHTML = `
        <div class="snippet-top">
          <div class="snippet-title">
            <span>${escapeHtml(s.title)}</span>
            <span class="snippet-cat">${escapeHtml(s.cat || 'Vorlagen')}</span>
            ${slotBadge}
          </div>
          <div style="display:flex; gap:6px;">
            <button class="btn btn-sm" data-edit="${s.id}" title="Bearbeiten">✎</button>
            <button class="btn btn-sm btn-danger" data-del="${s.id}" title="Löschen">🗑</button>
          </div>
        </div>
        <div class="snippet-preview">${escapeHtml(s.text)}</div>
        <div class="btn-row">
          <button class="btn btn-primary btn-sm" data-run="${s.id}" style="flex:2;">▶ Jetzt tippen${s.enter ? ' + ↵' : ''}</button>
          <button class="btn btn-sm" data-sync="${s.id}" style="flex:1;">💾 Auf Dongle</button>
        </div>
      `;

      card.querySelector(`[data-run="${s.id}"]`).addEventListener('click', () => {
        typeLongText(s.text, !!s.enter, false, 'dongle');
      });
      card.querySelector(`[data-edit="${s.id}"]`).addEventListener('click', () => openSnippetModal(s));
      card.querySelector(`[data-del="${s.id}"]`).addEventListener('click', () => {
        if (confirm(`Textmodul "${s.title}" wirklich löschen?`)) {
          snippets = snippets.filter((x) => x.id !== s.id);
          saveSnippets();
        }
      });
      card.querySelector(`[data-sync="${s.id}"]`).addEventListener('click', async () => {
        const defaultSlot = s.slot >= 0 ? s.slot : 0;
        const input = prompt('Auf welchen Dongle-Slot (0 bis 11) soll dieses Textmodul gespeichert werden?', String(defaultSlot));
        if (input === null) return;
        const slot = parseInt(input, 10);
        if (isNaN(slot) || slot < 0 || slot > 11) {
          alert('Bitte eine Slot-Nummer zwischen 0 und 11 eingeben.');
          return;
        }
        s.slot = slot;
        saveSnippets();
        const wireText = escapeForWire(s.enter ? s.text + '\n' : s.text);
        const safeTitle = s.title.replace(/\|/g, '-').slice(0, 28);
        await sendCommand(`SNIP SET ${slot}|${safeTitle}|${wireText}`);
        dongleSnippets = [];
        await sendCommand('SNIP LIST');
        showToast(`Auf Dongle-Slot #${slot} gespeichert!`);
      });

      list.appendChild(card);
    });
  }

  function renderDongleSnippets() {
    const list = $('dongleSnippetList');
    if (!list) return;
    list.innerHTML = '';

    if (dongleSnippets.length === 0) {
      list.innerHTML = '<div style="font-size:0.8rem; color:var(--text-muted);">Keine belegten Slots auf dem Dongle gefunden.</div>';
      return;
    }

    dongleSnippets.forEach((ds) => {
      const card = document.createElement('div');
      card.className = 'snippet-card';
      card.innerHTML = `
        <div class="snippet-top">
          <div class="snippet-title">
            <span class="snippet-cat" style="color:var(--accent)">Slot #${ds.slot}</span>
            <span>${escapeHtml(ds.title)}</span>
          </div>
          <button class="btn btn-sm btn-danger" data-ddel="${ds.slot}">✕</button>
        </div>
        <div class="snippet-preview">${escapeHtml(ds.text)}</div>
        <div class="btn-row">
          <button class="btn btn-accent btn-sm" data-drun="${ds.slot}" style="width:100%;">⚡ Direkt vom Dongle tippen (SNIP RUN ${ds.slot})</button>
        </div>
      `;

      card.querySelector(`[data-drun="${ds.slot}"]`).addEventListener('click', () => {
        haptic(15);
        sendCommand(`SNIP RUN ${ds.slot}`);
        showToast(`Dongle-Slot #${ds.slot} ausgeführt`);
      });

      card.querySelector(`[data-ddel="${ds.slot}"]`).addEventListener('click', async () => {
        if (confirm(`Dongle-Slot #${ds.slot} (${ds.title}) im NVS löschen?`)) {
          await sendCommand(`SNIP DEL ${ds.slot}`);
          dongleSnippets = [];
          await sendCommand('SNIP LIST');
        }
      });

      list.appendChild(card);
    });
  }

  function escapeHtml(str) {
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  function openSnippetModal(snip = null) {
    editingSnippetId = snip ? snip.id : null;
    $('modalTitle').textContent = snip ? 'Textmodul bearbeiten' : 'Neues Textmodul';
    $('modSnipTitle').value = snip ? snip.title : '';
    $('modSnipCat').value = snip ? snip.cat : 'Vorlagen';
    $('modSnipSlot').value = snip && snip.slot >= 0 ? String(snip.slot) : '-1';
    $('modSnipText').value = snip ? snip.text : '';
    $('modSnipEnter').checked = snip ? !!snip.enter : true;
    $('snippetModal').classList.add('open');
  }

  function closeSnippetModal() {
    $('snippetModal').classList.remove('open');
  }

  // --- Touchpad Implementation ---
  function initTouchpad() {
    const pad = $('touchpadArea');
    const strip = $('scrollStrip');
    const speedRange = $('rangeTpSensitivity');
    let speed = 1.6;

    speedRange.addEventListener('input', () => {
      speed = parseFloat(speedRange.value) || 1.6;
      $('tpSpeedBadge').textContent = `Speed: ${speed.toFixed(1)}x`;
    });

    let lastX = 0;
    let lastY = 0;
    let startX = 0;
    let startY = 0;
    let touchStartTime = 0;
    let maxTouches = 0;
    let lastTapTime = 0;
    let accumDx = 0;
    let accumDy = 0;
    let lastSendTime = 0;

    pad.addEventListener('touchstart', (e) => {
      e.preventDefault();
      maxTouches = Math.max(maxTouches, e.touches.length);
      if (e.touches.length === 1) {
        lastX = startX = e.touches[0].clientX;
        lastY = startY = e.touches[0].clientY;
        touchStartTime = performance.now();
      }
    }, { passive: false });

    pad.addEventListener('touchmove', (e) => {
      e.preventDefault();
      if (e.touches.length === 1 && maxTouches === 1) {
        const cx = e.touches[0].clientX;
        const cy = e.touches[0].clientY;
        accumDx += (cx - lastX) * speed;
        accumDy += (cy - lastY) * speed;
        lastX = cx;
        lastY = cy;

        const now = performance.now();
        if (now - lastSendTime >= 28 && !mouseBusy) {
          const ix = Math.max(-127, Math.min(127, Math.round(accumDx)));
          const iy = Math.max(-127, Math.min(127, Math.round(accumDy)));
          if (ix !== 0 || iy !== 0) {
            accumDx -= ix;
            accumDy -= iy;
            lastSendTime = now;
            mouseBusy = true;
            sendCommand(`M ${ix} ${iy}`, true).finally(() => { mouseBusy = false; });
          }
        }
      }
    }, { passive: false });

    pad.addEventListener('touchend', (e) => {
      e.preventDefault();
      if (e.touches.length === 0) {
        const dt = performance.now() - touchStartTime;
        const dist = Math.hypot(lastX - startX, lastY - startY);
        if (dt < 220 && dist < 10) {
          if (maxTouches === 2) {
            haptic(20);
            sendCommand('C R');
          } else if (maxTouches === 1) {
            const now = performance.now();
            if (now - lastTapTime < 280) {
              haptic(20);
              sendCommand('C DBL');
              lastTapTime = 0;
            } else {
              haptic(10);
              sendCommand('C L');
              lastTapTime = now;
            }
          }
        }
        maxTouches = 0;
        accumDx = 0;
        accumDy = 0;
      }
    }, { passive: false });

    // Scroll Strip
    let lastScrollY = 0;
    let accumScroll = 0;
    strip.addEventListener('touchstart', (e) => {
      e.preventDefault();
      lastScrollY = e.touches[0].clientY;
      accumScroll = 0;
    }, { passive: false });

    strip.addEventListener('touchmove', (e) => {
      e.preventDefault();
      const cy = e.touches[0].clientY;
      accumScroll += (lastScrollY - cy) / 18;
      lastScrollY = cy;
      const ticks = Math.trunc(accumScroll);
      if (ticks !== 0 && !mouseBusy) {
        accumScroll -= ticks;
        mouseBusy = true;
        sendCommand(`M 0 0 ${-ticks}`, true).finally(() => { mouseBusy = false; });
      }
    }, { passive: false });

    // Mouse buttons
    let dragActive = false;
    $('btnMouseLeft').addEventListener('click', () => { haptic(12); sendCommand('C L'); });
    $('btnMouseMiddle').addEventListener('click', () => { haptic(12); sendCommand('C M'); });
    $('btnMouseRight').addEventListener('click', () => { haptic(12); sendCommand('C R'); });
    $('btnMouseDrag').addEventListener('click', () => {
      dragActive = !dragActive;
      haptic(20);
      $('btnMouseDrag').classList.toggle('btn-primary', dragActive);
      sendCommand(dragActive ? 'C DOWN' : 'C UP');
    });
  }

  // --- Event Wiring ---
  function initEvents() {
    $('btnConnect').addEventListener('click', connectBle);

    // Header Status Pills (Optimistisches UI-Update + Befehl + Readback)
    $('pillJiggler').addEventListener('click', async () => {
      haptic(15);
      const nextState = deviceState.j ? 0 : 1;
      deviceState.j = nextState;
      updateStatusPills(false);
      await sendCommand(nextState ? 'J ON' : 'J OFF');
      await readStatusFromDongle(false, true);
    });

    $('pillLayout').addEventListener('click', async () => {
      haptic(15);
      const nextLay = deviceState.lay === 'DE' ? 'US' : 'DE';
      deviceState.lay = nextLay;
      updateStatusPills(true);
      await sendCommand(`SYS LAYOUT ${nextLay}`);
      showToast(`Layout: ${nextLay}-QWERT${nextLay === 'DE' ? 'Z' : 'Y'} gespeichert`);
      await readStatusFromDongle(false, true);
    });

    $('pillStealth').addEventListener('click', async () => {
      haptic(15);
      const nextStl = deviceState.stealth ? 0 : 1;
      deviceState.stealth = nextStl;
      updateStatusPills(false);
      await sendCommand(`SYS STEALTH ${nextStl}`);
      showToast(nextStl ? 'Stealth: Display & LED AUS' : 'Display & LED wieder AN');
      await readStatusFromDongle(false, true);
    });

    // Bottom Navigation Tabs
    document.querySelectorAll('.nav-item').forEach((btn) => {
      btn.addEventListener('click', () => {
        haptic(8);
        document.querySelectorAll('.nav-item').forEach((b) => b.classList.remove('active'));
        document.querySelectorAll('.tab-panel').forEach((p) => p.classList.remove('active'));
        btn.classList.add('active');
        $(btn.dataset.tab).classList.add('active');
      });
    });

    // Live Text Typer & Inline Pause Chips
    const liveInput = $('liveTextInput');
    liveInput.addEventListener('input', () => {
      $('charCountBadge').textContent = `${liveInput.value.length} Zeichen`;
    });

    document.querySelectorAll('[data-insert]').forEach((chip) => {
      chip.addEventListener('click', () => {
        haptic(10);
        insertAtCursor(liveInput, chip.dataset.insert);
      });
    });

    $('btnInsertCustomPause').addEventListener('click', () => {
      const msStr = prompt('Pause in Millisekunden eingeben (z.B. 250 oder 1500):', '500');
      if (!msStr) return;
      const ms = parseInt(msStr, 10);
      if (isNaN(ms) || ms < 1 || ms > 30000) {
        alert('Bitte einen Wert zwischen 1 und 30000 ms eingeben.');
        return;
      }
      insertAtCursor(liveInput, `{W:${ms}}`);
    });

    $('selLiveTimingMode').addEventListener('change', () => {
      const isCustom = $('selLiveTimingMode').value === 'custom';
      $('wrapCustomCharPause').style.display = isCustom ? 'block' : 'none';
    });

    document.querySelectorAll('[data-mod-insert]').forEach((chip) => {
      chip.addEventListener('click', () => {
        haptic(10);
        insertAtCursor($('modSnipText'), chip.dataset.modInsert);
      });
    });

    $('btnSendLiveText').addEventListener('click', () => {
      const txt = liveInput.value;
      if (!txt) {
        showToast('Bitte zuerst Text eingeben');
        return;
      }
      typeLongText(
        txt,
        $('chkAppendEnter').checked,
        $('chkClearBefore').checked,
        $('selLiveTimingMode').value
      );
    });

    $('btnPasteAndType').addEventListener('click', async () => {
      try {
        const clip = await navigator.clipboard.readText();
        if (!clip) {
          showToast('Zwischenablage ist leer');
          return;
        }
        liveInput.value = clip;
        $('charCountBadge').textContent = `${clip.length} Zeichen`;
        await typeLongText(
          clip,
          $('chkAppendEnter').checked,
          $('chkClearBefore').checked,
          $('selLiveTimingMode').value
        );
      } catch (_) {
        showToast('Bitte Text direkt in das Feld einfügen');
      }
    });

    $('btnClearLiveText').addEventListener('click', () => {
      liveInput.value = '';
      $('charCountBadge').textContent = '0 Zeichen';
    });

    // Category Chips
    document.querySelectorAll('#categoryChips .chip').forEach((chip) => {
      chip.addEventListener('click', () => {
        document.querySelectorAll('#categoryChips .chip').forEach((c) => c.classList.remove('active'));
        chip.classList.add('active');
        activeCategory = chip.dataset.cat;
        renderSnippets();
      });
    });

    // Snippet Modal & Export/Import
    $('btnNewSnippet').addEventListener('click', () => openSnippetModal(null));
    $('btnCloseModal').addEventListener('click', closeSnippetModal);
    $('btnSaveModalSnippet').addEventListener('click', async () => {
      const title = $('modSnipTitle').value.trim();
      const text = $('modSnipText').value;
      const cat = $('modSnipCat').value;
      const slot = parseInt($('modSnipSlot').value, 10);
      const enter = $('modSnipEnter').checked;

      if (!title || !text) {
        alert('Bitte Titel und Text eingeben.');
        return;
      }

      if (editingSnippetId) {
        const idx = snippets.findIndex((x) => x.id === editingSnippetId);
        if (idx >= 0) {
          snippets[idx] = { ...snippets[idx], title, text, cat, slot, enter };
        }
      } else {
        snippets.unshift({
          id: 's_' + Date.now(),
          title,
          text,
          cat,
          slot,
          enter
        });
      }
      saveSnippets();
      closeSnippetModal();

      if (slot >= 0 && isConnected()) {
        const wireText = escapeForWire(enter ? text + '\n' : text);
        const safeTitle = title.replace(/\|/g, '-').slice(0, 28);
        await sendCommand(`SNIP SET ${slot}|${safeTitle}|${wireText}`);
        dongleSnippets = [];
        await sendCommand('SNIP LIST');
      }
      showToast('Textmodul gespeichert');
    });

    $('btnExportSnippets').addEventListener('click', () => {
      const blob = new Blob([JSON.stringify(snippets, null, 2)], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = 'tdongle-textmodule.json';
      a.click();
      URL.revokeObjectURL(url);
    });

    $('btnImportSnippets').addEventListener('click', () => $('fileImportSnippets').click());
    $('fileImportSnippets').addEventListener('change', (e) => {
      const file = e.target.files[0];
      if (!file) return;
      const reader = new FileReader();
      reader.onload = () => {
        try {
          const arr = JSON.parse(reader.result);
          if (Array.isArray(arr)) {
            snippets = arr;
            saveSnippets();
            showToast(`${arr.length} Textmodule importiert`);
          }
        } catch (_) {
          alert('Ungültige JSON-Datei');
        }
      };
      reader.readAsText(file);
    });

    $('btnRefreshDongleSnips').addEventListener('click', async () => {
      dongleSnippets = [];
      await sendCommand('SNIP LIST');
    });

    // All Combo & Media Buttons
    document.querySelectorAll('[data-combo]').forEach((btn) => {
      btn.addEventListener('click', () => {
        haptic(14);
        sendCommand(`K ${btn.dataset.combo}`);
      });
    });

    document.querySelectorAll('[data-media]').forEach((btn) => {
      btn.addEventListener('click', () => {
        haptic(14);
        sendCommand(`MEDIA ${btn.dataset.media}`);
      });
    });

    $('btnFireCustomCombo').addEventListener('click', () => {
      const mods = Array.from(document.querySelectorAll('.mod-chk:checked')).map((c) => c.value);
      const key = $('selCustomKey').value;
      const combo = [...mods, key].join('+');
      haptic(15);
      sendCommand(`K ${combo}`);
      showToast(`Gesendet: ${combo}`);
    });

    // Jiggler & System Controls
    $('btnJigglerOn').addEventListener('click', async () => {
      haptic(15);
      deviceState.j = 1;
      updateStatusPills(false);
      await sendCommand('J ON');
      await readStatusFromDongle(false, true);
    });

    $('btnJigglerOff').addEventListener('click', async () => {
      haptic(15);
      deviceState.j = 0;
      updateStatusPills(false);
      await sendCommand('J OFF');
      await readStatusFromDongle(false, true);
    });

    $('btnSaveJiggleCfg').addEventListener('click', async () => {
      const minS = parseInt($('inpJiggleMin').value, 10) || 30;
      const maxS = parseInt($('inpJiggleMax').value, 10) || 60;
      const mode = parseInt($('selJiggleMode').value, 10) || 0;
      const auto = $('chkJiggleAuto').checked ? 1 : 0;
      haptic(18);
      await sendCommand(`JCFG ${minS} ${maxS} ${mode} ${auto}`);
      showToast('Jiggler-Konfiguration im NVS gespeichert');
      await readStatusFromDongle(false, true);
    });

    $('btnSaveKeyboardCfg').addEventListener('click', async () => {
      const lay = $('selLayout').value;
      const minD = Math.max(4, Math.min(500, parseInt($('inpKeyDelay').value, 10) || 12));
      const rnd = $('chkRandomDelay').checked ? 1 : 0;
      const maxD = Math.max(minD, Math.min(1000, parseInt($('inpKeyDelayMax').value, 10) || 55));
      haptic(18);
      deviceState.lay = lay;
      deviceState.delay = minD;
      deviceState.rnd = rnd;
      deviceState.rmax = maxD;
      updateStatusPills(false);
      await sendCommand(`SYS LAYOUT ${lay}`);
      await sendCommand(`SYS DELAY ${minD} ${rnd} ${maxD}`);
      showToast('Tastaturlayout & Verzögerung im NVS gespeichert');
      await readStatusFromDongle(false, true);
    });

    $('btnDisplayOn').addEventListener('click', async () => {
      haptic(15);
      deviceState.stealth = 0;
      updateStatusPills(false);
      await sendCommand('SYS STEALTH 0');
      showToast('Display & LED eingeschaltet');
      await readStatusFromDongle(false, true);
    });

    $('btnDisplayOff').addEventListener('click', async () => {
      haptic(15);
      deviceState.stealth = 1;
      updateStatusPills(false);
      await sendCommand('SYS STEALTH 1');
      showToast('Stealth: Display & LED ausgeschaltet');
      await readStatusFromDongle(false, true);
    });

    $('btnSavePin').addEventListener('click', async () => {
      const pin = parseInt($('inpBlePin').value, 10);
      if (isNaN(pin) || pin < 100000 || pin > 999999) {
        alert('Bitte eine 6-stellige PIN zwischen 100000 und 999999 eingeben.');
        return;
      }
      await sendCommand(`SYS PIN ${pin}`);
      showToast(`Neue feste PIN: ${pin}`);
      await readStatusFromDongle(false, true);
    });

    $('btnRequestStatus').addEventListener('click', async () => {
      await sendCommand('SYS STATUS');
      await readStatusFromDongle(true, true);
    });

    $('btnBootloader').addEventListener('click', () => {
      if (confirm('T-Dongle S3 jetzt in den USB-Flash-Modus (Bootloader) neustarten?\n\nDer Dongle meldet sich danach als COM-Port am PC an.')) {
        sendCommand('SYS BOOTLOADER');
      }
    });

    const btnJumpVoice = $('btnJumpToVoice');
    if (btnJumpVoice) {
      btnJumpVoice.addEventListener('click', () => {
        haptic(10);
        document.querySelectorAll('.nav-item').forEach((b) => {
          b.classList.toggle('active', b.dataset.tab === 'tab-voice');
        });
        document.querySelectorAll('.tab-panel').forEach((p) => {
          p.classList.toggle('active', p.id === 'tab-voice');
        });
      });
    }
  }

  // =========================================================================
  // GEMINI LIVE-DIKTAT & SMART VOICE ENGINE (Pixel 10 Pro XL -> T-Dongle S3)
  // =========================================================================
  const GEMINI_CFG_KEY = 'tdongle_gemini_cfg_v1';

  let geminiCfg = {
    apiKey: '',
    engine: 'smart_vad',
    model: '',
    customModel: '',
    style: 'orig',
    clean: true,
    commands: true,
    autoSpace: true,
    vadSilenceMs: 800,
    noiseGatePct: 28,
    outputMode: 'live', // 'live' | 'preview'
    cachedModels: [],
    totalAudioSec: 0,
    totalTokens: 0
  };

  let voiceActive = false;
  let audioCtx = null;
  let mediaStream = null;
  let scriptNode = null;
  let liveWs = null;
  let preRollChunks = [];
  let vadActiveChunks = [];
  let vadActiveSampleCount = 0;
  let vadVoicedSampleCount = 0;
  let vadSpeaking = false;
  let vadLastVoiceAt = 0;
  let vadSegmentStartAt = 0;
  let wsChunkAccum = [];
  let wsChunkSamples = 0;
  let pendingGeminiTasks = 0;
  let voiceQueue = Promise.resolve();

  function loadGeminiCfg() {
    try {
      const raw = localStorage.getItem(GEMINI_CFG_KEY);
      if (raw) {
        const parsed = JSON.parse(raw);
        geminiCfg = { ...geminiCfg, ...parsed };
        if (!geminiCfg.noiseGatePct || geminiCfg.noiseGatePct < 8) {
          geminiCfg.noiseGatePct = 28;
        }
      }
    } catch (_) {}
  }

  function saveGeminiCfg() {
    try {
      localStorage.setItem(GEMINI_CFG_KEY, JSON.stringify(geminiCfg));
    } catch (_) {}
  }

  function updateUsageStatsUi() {
    const el = $('voiceUsageStats');
    if (!el) return;
    const sec = (geminiCfg.totalAudioSec || 0).toFixed(1);
    const tok = Math.round(geminiCfg.totalTokens || 0);
    const usd = (tok * 0.00000015).toFixed(4);
    el.textContent = `📊 Verbrauch: ${sec}s Audio • ~${tok} Tokens • ~$${usd} (Antippen = Reset)`;
  }

  function getSelectedGeminiModel() {
    const custom = ($('inpCustomGeminiModel').value || '').trim();
    if (custom) {
      return custom.replace(/^models\//, '');
    }
    const selVal = ($('selGeminiModel').value || '').trim().replace(/^models\//, '');
    if (selVal) return selVal;
    if (Array.isArray(geminiCfg.cachedModels) && geminiCfg.cachedModels.length > 0) {
      return geminiCfg.cachedModels[0].id;
    }
    return 'gemini-2.5-flash';
  }

  function getFallback3xModel(excludeModel) {
    if (!Array.isArray(geminiCfg.cachedModels) || geminiCfg.cachedModels.length === 0) {
      return null;
    }
    const candidates = geminiCfg.cachedModels.filter(
      (m) => m.id !== excludeModel && (m.id.includes('flash-lite') || m.id.includes('flash'))
    );
    return candidates.length > 0 ? candidates[0].id : null;
  }

  function setVoiceStatus(label, isError = false) {
    const b = $('voiceStatusBadge');
    if (!b) return;
    b.textContent = label;
    b.style.color = isError ? 'var(--danger)' : voiceActive ? 'var(--primary)' : 'var(--text-muted)';
  }

  function setVoiceInterim(msg) {
    const el = $('voiceInterimLine');
    if (el) el.textContent = msg;
  }

  function uint8ToBase64(bytes) {
    let binary = '';
    const step = 0x8000;
    for (let i = 0; i < bytes.length; i += step) {
      binary += String.fromCharCode.apply(null, bytes.subarray(i, i + step));
    }
    return btoa(binary);
  }

  // Downsampling von Browser-Audio (z.B. 48000 Hz Float32) auf 16000 Hz Int16 PCM
  function downsampleTo16kPcm(float32Buf, inputRate) {
    const targetRate = 16000;
    if (inputRate === targetRate) {
      const out = new Int16Array(float32Buf.length);
      for (let i = 0; i < float32Buf.length; i++) {
        const s = Math.max(-1, Math.min(1, float32Buf[i]));
        out[i] = s < 0 ? s * 0x8000 : s * 0x7FFF;
      }
      return out;
    }
    const ratio = inputRate / targetRate;
    const outLen = Math.round(float32Buf.length / ratio);
    const out = new Int16Array(outLen);
    let offsetResult = 0;
    let offsetBuffer = 0;
    while (offsetResult < outLen) {
      const nextOffsetBuffer = Math.round((offsetResult + 1) * ratio);
      let accum = 0;
      let count = 0;
      for (let i = offsetBuffer; i < nextOffsetBuffer && i < float32Buf.length; i++) {
        accum += float32Buf[i];
        count++;
      }
      const sample = count > 0 ? Math.max(-1, Math.min(1, accum / count)) : 0;
      out[offsetResult] = sample < 0 ? sample * 0x8000 : sample * 0x7FFF;
      offsetResult++;
      offsetBuffer = nextOffsetBuffer;
    }
    return out;
  }

  // Erzeugt aus einem Array von Int16Array-Chunks ein valides 16-kHz-Mono-WAV im RAM
  function encodeWavBase64(int16Chunks, sampleRate = 16000) {
    let totalSamples = 0;
    for (const c of int16Chunks) totalSamples += c.length;
    const dataBytes = totalSamples * 2;
    const buf = new ArrayBuffer(44 + dataBytes);
    const view = new DataView(buf);

    const writeAscii = (off, str) => {
      for (let i = 0; i < str.length; i++) view.setUint8(off + i, str.charCodeAt(i));
    };

    writeAscii(0, 'RIFF');
    view.setUint32(4, 36 + dataBytes, true);
    writeAscii(8, 'WAVE');
    writeAscii(12, 'fmt ');
    view.setUint32(16, 16, true); // PCM
    view.setUint16(20, 1, true);  // Linear PCM
    view.setUint16(22, 1, true);  // 1 Kanal (Mono)
    view.setUint32(24, sampleRate, true);
    view.setUint32(28, sampleRate * 2, true);
    view.setUint16(32, 2, true);
    view.setUint16(34, 16, true); // 16-bit
    writeAscii(36, 'data');
    view.setUint32(40, dataBytes, true);

    const outInt16 = new Int16Array(buf, 44, totalSamples);
    let pos = 0;
    for (const c of int16Chunks) {
      outInt16.set(c, pos);
      pos += c.length;
    }
    return uint8ToBase64(new Uint8Array(buf));
  }

  function encodePcmBase64(int16Chunks) {
    let totalSamples = 0;
    for (const c of int16Chunks) totalSamples += c.length;
    const buf = new ArrayBuffer(totalSamples * 2);
    const outInt16 = new Int16Array(buf);
    let pos = 0;
    for (const c of int16Chunks) {
      outInt16.set(c, pos);
      pos += c.length;
    }
    return uint8ToBase64(new Uint8Array(buf));
  }

  // Konvertiert gesprochene Steuerbefehle zuverlässig in Dongle-HID-Tags
  function applySpokenCommands(rawText) {
    let t = rawText;
    if (!geminiCfg.commands) return t;
    t = t
      .replace(/\b(neue zeile|zeilenumbruch|enter drücken|eingabetaste)\b/gi, '{ENTER}')
      .replace(/\b(neuer absatz|absatz einfügen)\b/gi, '{ENTER}{ENTER}')
      .replace(/\b(tabulator|tab drücken|nächstes feld)\b/gi, '{TAB}')
      .replace(/\b(rückwärtstaste|zeichen löschen|backspace)\b/gi, '{BS}');
    return t;
  }

  function buildSystemInstruction() {
    const parts = [
      'Du bist eine professionelle KI-Diktier-Engine für eine PC-Tastatur.',
      'WICHTIG: Gib AUSSCHLIESSLICH den fertigen Diktat-Text zurück – keinerlei Einleitungen, keine Anführungszeichen, keine Metakommentare.',
      'STRIKTER STÖRFILTER: Wenn in der Audioaufnahme nur Stille, Atmen, Räuspern, Husten, Tastaturklappern, Rascheln oder unverständliches Gemurmel ohne klar gesprochene Wörter zu hören sind, antworte AUSSCHLIESSLICH mit dem exakten Wort [STILLE]. Erfinde oder errate NIEMALS Wörter aus Nebengeräuschen!'
    ];

    if (geminiCfg.clean) {
      parts.push(
        'STRIKTE FÜLLWORT-BEREINIGUNG & SATZGLÄTTUNG: Schreibe NIEMALS Verzögerungslaute wie "äh", "ähm", "hm", "mhm" mit! Entferne alle Fülllaute, Stotterer, Wortdopplungen und abgebrochenen Satzanfänge restlos. Wenn der Sprecher sich mitten im Satz korrigiert oder holprig spricht, glätte das Gesprochene zu einem grammatikalisch sauberen, flüssigen Satz mit perfekter Rechtschreibung, Groß-/Kleinschreibung und Kommasetzung.'
      );
    } else {
      parts.push('Transkribiere die gesprochenen Wörter mit korrekter Rechtschreibung und Satzzeichen.');
    }

    if (geminiCfg.commands) {
      parts.push('Wenn der Sprecher explizit die Kommandos "Neue Zeile" oder "Zeilenumbruch" sagt, schreibe stattdessen exakt den Tag {ENTER}. Bei "Neuer Absatz" schreibe {ENTER}{ENTER}. Bei "Tabulator" schreibe {TAB}.');
    }

    switch (geminiCfg.style) {
      case 'en':
        parts.push('Übersetze das Gesprochene direkt in natürliches, flüssiges Englisch.');
        break;
      case 'fr':
        parts.push('Übersetze das Gesprochene direkt in natürliches, flüssiges Französisch.');
        break;
      case 'es':
        parts.push('Übersetze das Gesprochene direkt in natürliches, flüssiges Spanisch.');
        break;
      case 'it':
        parts.push('Übersetze das Gesprochene direkt in natürliches, flüssiges Italienisch.');
        break;
      case 'mail_de':
        parts.push('Formuliere das Gesprochene direkt als professionellen, höflichen deutschen Business-/E-Mail-Text.');
        break;
      case 'mail_en':
        parts.push('Übersetze und formuliere das Gesprochene direkt als professionellen, höflichen englischen Business-/E-Mail-Text.');
        break;
      default:
        parts.push('Behalte die gesprochene Sprache (standardmäßig Deutsch) bei.');
        break;
    }

    return parts.join(' ');
  }

  async function handleRecognizedVoiceSegment(rawText) {
    let cleaned = (rawText || '').trim();
    if (!cleaned) return;

    // Anti-Halluzinations-Tag und reine Geräusch-Platzhalter verwerfen
    if (
      cleaned.toUpperCase().includes('[STILLE]') ||
      cleaned.toUpperCase() === 'STILLE' ||
      cleaned === '.' ||
      cleaned === '...'
    ) {
      if (voiceActive) {
        setVoiceInterim('🔇 Nebengeräusch / Stille herausgefiltert – sprich weiter…');
      }
      return;
    }

    // Zusätzlicher Client-Sicherheitsfilter gegen übriggebliebene "äh" / "ähm"
    if (geminiCfg.clean) {
      cleaned = cleaned
        .replace(/(^|\s)(ähm+|äh+|hm+|mhm+)(?=\s|[,.!?;:]|$)[,\s]*/gi, '$1')
        .replace(/\s{2,}/g, ' ')
        .replace(/\s+([,.!?;:])/g, '$1')
        .trim();
    }

    if (!cleaned) return;

    cleaned = applySpokenCommands(cleaned);
    const needSpace = geminiCfg.autoSpace && !cleaned.endsWith('{ENTER}') && !cleaned.endsWith('{TAB}');
    const finalOut = needSpace ? cleaned + ' ' : cleaned;

    const box = $('voiceTranscriptBox');
    if (box) {
      const displayStr = finalOut
        .replace(/\{ENTER\}/g, '\n')
        .replace(/\{TAB\}/g, '\t')
        .replace(/\{BS\}/g, '');
      box.value += displayStr;
      box.scrollTop = box.scrollHeight;
    }

    setVoiceInterim(`✓ "${cleaned.slice(0, 68)}${cleaned.length > 68 ? '…' : ''}"`);

    if (geminiCfg.outputMode === 'live') {
      if (isConnected()) {
        const escaped = escapeForWire(finalOut);
        await sendCommand(`T ${escaped}`);
      } else {
        showToast('Im Textfeld erfasst (Dongle nicht per BLE verbunden)');
      }
    }
  }

  function dispatchVadAudioSegment(chunksToProcess) {
    if (!chunksToProcess || chunksToProcess.length === 0) return;
    let totalSamples = 0;
    for (const c of chunksToProcess) totalSamples += c.length;
    const durNum = totalSamples / 16000;
    const durSec = durNum.toFixed(1);

    const wavB64 = encodeWavBase64(chunksToProcess, 16000);
    const apiKey = geminiCfg.apiKey.trim();
    const primaryModel = getSelectedGeminiModel();
    const sysPrompt = buildSystemInstruction();

    pendingGeminiTasks++;
    setVoiceStatus(`Gemini (${pendingGeminiTasks})…`);
    setVoiceInterim(`⚡ Sende ${durSec}s Sprache an ${primaryModel}…`);

    voiceQueue = voiceQueue
      .then(async () => {
        const callGeminiAudio = async (mdl) => {
          const url = `https://generativelanguage.googleapis.com/v1beta/models/${encodeURIComponent(mdl)}:generateContent?key=${encodeURIComponent(apiKey)}`;
          const body = {
            systemInstruction: {
              parts: [{ text: sysPrompt }]
            },
            contents: [
              {
                role: 'user',
                parts: [
                  {
                    inlineData: {
                      mimeType: 'audio/wav',
                      data: wavB64
                    }
                  },
                  {
                    text: 'Diktat-Audio verarbeiten gemäß System-Anweisung (bei Geräusch/Stille: [STILLE]).'
                  }
                ]
              }
            ],
            generationConfig: {
              temperature: 0.1
            }
          };

          const resp = await fetch(url, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body)
          });

          if (!resp.ok) {
            const errJson = await resp.json().catch(() => ({}));
            const errMsg = (errJson.error && errJson.error.message) || `HTTP ${resp.status}`;
            throw new Error(errMsg);
          }
          return resp.json();
        };

        try {
          let data;
          try {
            data = await callGeminiAudio(primaryModel);
          } catch (firstErr) {
            const altModel = getFallback3xModel(primaryModel);
            if (altModel) {
              addLog('RX', `Fallback von ${primaryModel} auf ${altModel} (${firstErr.message})`);
              data = await callGeminiAudio(altModel);
            } else {
              throw firstErr;
            }
          }

          // Audio-Sekunden & geschätzte Tokens verbuchen (~32 Tokens/Sekunde Audio + Prompt/Output)
          const usedTokens = data.usageMetadata?.totalTokenCount || Math.round(durNum * 32 + 95);
          geminiCfg.totalAudioSec = (geminiCfg.totalAudioSec || 0) + durNum;
          geminiCfg.totalTokens = (geminiCfg.totalTokens || 0) + usedTokens;
          saveGeminiCfg();
          updateUsageStatsUi();

          const cand = data.candidates && data.candidates[0];
          const parts = cand && cand.content && cand.content.parts;
          const text = parts ? parts.map((p) => p.text || '').join('').trim() : '';

          if (text) {
            await handleRecognizedVoiceSegment(text);
          } else if (voiceActive) {
            setVoiceInterim('🎙️ Keine Sprache im Abschnitt erkannt – sprich weiter…');
          }
        } catch (err) {
          addLog('ERR', `Gemini: ${err.message || err}`);
          showToast(`Gemini-Fehler: ${err.message || err}`);
          setVoiceStatus('API-Fehler', true);
          setVoiceInterim(`❌ Fehler: ${err.message || err}`);
        } finally {
          pendingGeminiTasks = Math.max(0, pendingGeminiTasks - 1);
          if (pendingGeminiTasks === 0) {
            setVoiceStatus(voiceActive ? 'Hört zu…' : 'Bereit');
          } else {
            setVoiceStatus(`Gemini (${pendingGeminiTasks})…`);
          }
        }
      })
      .catch(() => {});
  }

  function flushCurrentVadSegment(forceEvenShort = false) {
    // Mindestens 0.45 Sekunden echte Sprach-Samples über dem Noise-Gate (7200 Samples @ 16kHz),
    // damit kurze Tippgeräusche, Atmen oder Knackser gar nicht erst an die API geschickt werden!
    const minVoicedSamples = forceEvenShort ? 2400 : 7200;
    if (vadActiveChunks.length > 0 && vadVoicedSampleCount >= minVoicedSamples) {
      const copy = vadActiveChunks;
      vadActiveChunks = [];
      vadActiveSampleCount = 0;
      vadVoicedSampleCount = 0;
      vadSpeaking = false;
      dispatchVadAudioSegment(copy);
    } else {
      if (vadSpeaking && vadVoicedSampleCount > 0) {
        setVoiceInterim('🔇 Kurzes Nebengeräusch (< 0.45s) ignoriert – spart API-Tokens…');
      }
      vadActiveChunks = [];
      vadActiveSampleCount = 0;
      vadVoicedSampleCount = 0;
      vadSpeaking = false;
    }
  }

  // WebSocket Live API (BidiGenerateContent) Verbindung mit 2-Stufen-Handshake (AUDIO+inputAudioTranscription -> TEXT)
  function openSingleLiveWsAttempt(apiKey, model, useAudioModality) {
    return new Promise((resolve, reject) => {
      const wsUrl = `wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent?key=${encodeURIComponent(apiKey)}`;
      const ws = new WebSocket(wsUrl);
      let settled = false;

      const finishResolve = () => {
        if (settled) return;
        settled = true;
        clearTimeout(timeoutId);
        resolve(ws);
      };

      const finishReject = (err) => {
        if (settled) return;
        settled = true;
        clearTimeout(timeoutId);
        try { ws.close(); } catch (_) {}
        reject(err);
      };

      const timeoutId = setTimeout(() => {
        finishReject(new Error(`Modell "${model}" antwortet nicht auf Bidi-WebSocket`));
      }, 2500);

      ws.onopen = () => {
        const setupMsg = {
          setup: {
            model: `models/${model}`,
            generationConfig: {
              responseModalities: useAudioModality ? ['AUDIO'] : ['TEXT']
            },
            systemInstruction: {
              parts: [{ text: buildSystemInstruction() }]
            },
            ...(useAudioModality ? { inputAudioTranscription: {} } : {})
          }
        };
        ws.send(JSON.stringify(setupMsg));
      };

      ws.onmessage = async (evt) => {
        try {
          let rawStr = '';
          if (typeof evt.data === 'string') {
            rawStr = evt.data;
          } else if (evt.data instanceof Blob) {
            rawStr = await evt.data.text();
          } else if (evt.data instanceof ArrayBuffer) {
            rawStr = new TextDecoder('utf-8').decode(evt.data);
          }
          if (!rawStr) return;
          const msg = JSON.parse(rawStr);

          if (msg.setupComplete !== undefined && !settled) {
            finishResolve();
            return;
          }

          if (msg.error) {
            finishReject(new Error(msg.error.message || 'Live-WS Fehler'));
            return;
          }

          const sc = msg.serverContent;
          if (sc) {
            if (sc.inputTranscription && sc.inputTranscription.text) {
              await handleRecognizedVoiceSegment(sc.inputTranscription.text);
            } else if (sc.modelTurn && Array.isArray(sc.modelTurn.parts)) {
              const txt = sc.modelTurn.parts.map((p) => p.text || '').join('').trim();
              if (txt) await handleRecognizedVoiceSegment(txt);
            }
          }
        } catch (_) {}
      };

      ws.onerror = () => {
        finishReject(new Error('Live-WebSocket Verbindungsfehler'));
      };

      ws.onclose = (ev) => {
        if (!settled) {
          finishReject(new Error(`Code ${ev.code}${ev.reason ? ': ' + ev.reason : ''}`));
          return;
        }
        if (voiceActive && liveWs === ws) {
          addLog('RX', `Live-WS geschlossen (${ev.code}) -> führe Sitzung als Turbo-Live-Chunks fort`);
          liveWs = null;
          showToast('Live-WS vom Server getrennt -> läuft als Turbo-Stream weiter');
        }
      };
    });
  }

  async function startLiveWebSocket(apiKey, model) {
    try {
      // 1. Versuch: Offizielles BidiGenerateContent AUDIO + inputAudioTranscription Setup (für TranscribeLive & Native-Audio)
      return await openSingleLiveWsAttempt(apiKey, model, true);
    } catch (firstErr) {
      // 2. Versuch: Fallback auf TEXT-Modalität für ältere Bidi-Modelle
      return await openSingleLiveWsAttempt(apiKey, model, false);
    }
  }

  async function startVoiceDictation() {
    syncGeminiCfgFromUi();
    if (!geminiCfg.apiKey) {
      showToast('Bitte unten zuerst deinen Gemini API-Key eintragen!');
      $('inpGeminiKey').focus();
      return;
    }

    try {
      setVoiceStatus('Starte Mikrofon…');
      setVoiceInterim('🎙️ Öffne Mikrofon auf dem Pixel…');
      mediaStream = await navigator.mediaDevices.getUserMedia({
        audio: {
          channelCount: 1,
          echoCancellation: true,
          noiseSuppression: true,
          autoGainControl: true
        }
      });

      const AudioCtx = window.AudioContext || window.webkitAudioContext;
      audioCtx = new AudioCtx();
      if (audioCtx.state === 'suspended') {
        await audioCtx.resume();
      }
      const inputRate = audioCtx.sampleRate || 48000;

      let activeSessionEngine = geminiCfg.engine;
      if (activeSessionEngine === 'live_ws') {
        const chosenModel = getSelectedGeminiModel();
        setVoiceStatus('Verbinde Live-WS…');
        try {
          liveWs = await startLiveWebSocket(geminiCfg.apiKey.trim(), chosenModel);
          showToast(`Live-WebSocket verbunden (${chosenModel}) ✓`);
        } catch (wsErr) {
          // Modell erlaubt bei Google kein Bidi-WebSocket -> nutze für diese Sitzung den schnellen 2s-Turbo-Chunk-Modus,
          // OHNE das Dropdown des Nutzers heimlich zu verstellen!
          addLog('RX', `Modell "${chosenModel}" lehnt WebSocket ab (${wsErr.message}) -> nutze 2s-Turbo-Chunks`);
          activeSessionEngine = 'turbo_chunk';
          liveWs = null;
          showToast(`Hinweis: ${chosenModel} unterstützt kein WS -> nutze 2s-Turbo-Stream!`);
        }
      }

      const source = audioCtx.createMediaStreamSource(mediaStream);
      scriptNode = audioCtx.createScriptProcessor(4096, 1, 1);

      preRollChunks = [];
      vadActiveChunks = [];
      vadActiveSampleCount = 0;
      vadVoicedSampleCount = 0;
      vadSpeaking = false;
      wsChunkAccum = [];
      wsChunkSamples = 0;
      voiceActive = true;

      $('btnVoiceToggle').classList.add('active');
      $('voiceCockpit').classList.add('recording');
      $('voiceMicIcon').textContent = '⏹️';
      $('voiceBtnLabel').textContent = 'Diktat stoppen (läuft live)';
      setVoiceStatus(activeSessionEngine === 'live_ws' ? 'Live-WS aktiv' : activeSessionEngine === 'turbo_chunk' ? 'Turbo-Live…' : 'Hört zu…');
      setVoiceInterim('🎙️ Mikrofon aktiv – sprich über den roten Strich im Pegelbalken…');
      haptic(25);

      scriptNode.onaudioprocess = (audioEvt) => {
        if (!voiceActive) return;
        const inputBuf = audioEvt.inputBuffer.getChannelData(0);

        let sumSq = 0;
        for (let i = 0; i < inputBuf.length; i++) {
          sumSq += inputBuf[i] * inputBuf[i];
        }
        const rms = Math.sqrt(sumSq / inputBuf.length);
        // Kalibrierte Pegel-Skala für Pixel 10 Pro XL (normale Stimme bei ~50-75% statt Anschlag)
        const pct = Math.min(100, Math.round(Math.pow(rms * 6.2, 0.78) * 100));
        const bar = $('voiceLevelBar');
        if (bar) bar.style.width = `${pct}%`;

        const pcm16 = downsampleTo16kPcm(inputBuf, inputRate);
        const now = performance.now();

        if (activeSessionEngine === 'live_ws' && liveWs && liveWs.readyState === WebSocket.OPEN) {
          // Nur senden wenn über Noise-Gate oder kurz nach Sprache (spart Tokens bei Stille)
          const gatePct = geminiCfg.noiseGatePct || 42;
          if (pct >= gatePct) {
            vadLastVoiceAt = now;
          }
          if (pct >= gatePct || now - vadLastVoiceAt < 1200) {
            wsChunkAccum.push(pcm16);
            wsChunkSamples += pcm16.length;
            if (wsChunkSamples >= 2400) {
              const b64 = encodePcmBase64(wsChunkAccum);
              const sentSec = wsChunkSamples / 16000;
              wsChunkAccum = [];
              wsChunkSamples = 0;
              geminiCfg.totalAudioSec = (geminiCfg.totalAudioSec || 0) + sentSec;
              geminiCfg.totalTokens = (geminiCfg.totalTokens || 0) + Math.round(sentSec * 32);
              updateUsageStatsUi();
              liveWs.send(
                JSON.stringify({
                  realtimeInput: {
                    mediaChunks: [
                      {
                        mimeType: 'audio/pcm;rate=16000',
                        data: b64
                      }
                    ]
                  }
                })
              );
            }
          }
          return;
        }

        // Engine: smart_vad (Satzweise) ODER turbo_chunk (Alle ~2.2s laufend während des Redens)
        const isTurbo = activeSessionEngine === 'turbo_chunk';
        const gatePct = geminiCfg.noiseGatePct || 42;
        if (pct >= gatePct) {
          if (!vadSpeaking) {
            vadSpeaking = true;
            vadSegmentStartAt = now;
            vadVoicedSampleCount = 0;
            for (const pr of preRollChunks) {
              vadActiveChunks.push(pr);
              vadActiveSampleCount += pr.length;
            }
            preRollChunks = [];
          }
          vadLastVoiceAt = now;
          vadActiveChunks.push(pcm16);
          vadActiveSampleCount += pcm16.length;
          vadVoicedSampleCount += pcm16.length;

          const voicedSec = (vadVoicedSampleCount / 16000).toFixed(1);
          setVoiceInterim(
            isTurbo
              ? `⚡ Turbo-Stream (${voicedSec}s) – sendet laufend beim Sprechen…`
              : `🗣️ Sprache aktiv (${voicedSec}s über Gate) – kurz pausieren zum Senden…`
          );

          // Im Turbo-Modus alle 2.8s (bzw. im Satz-Modus nach 7s) automatisch zwischensenden
          const maxContinuousMs = isTurbo ? 2800 : 7000;
          if (now - vadSegmentStartAt >= maxContinuousMs) {
            flushCurrentVadSegment(isTurbo);
          }
        } else {
          if (vadSpeaking) {
            vadActiveChunks.push(pcm16);
            vadActiveSampleCount += pcm16.length;
            // Im Turbo-Modus reicht schon eine Mini-Wortlücke von 220ms (ab 1.4s Redezeit) zum Live-Senden!
            const silenceNeeded =
              isTurbo && vadVoicedSampleCount >= 22000
                ? 220
                : isTurbo
                ? 420
                : geminiCfg.vadSilenceMs;
            if (now - vadLastVoiceAt >= silenceNeeded) {
              flushCurrentVadSegment(isTurbo);
            }
          } else {
            // Im Ruhezustand die letzten ~350ms als Pre-Roll im Ringpuffer behalten
            preRollChunks.push(pcm16);
            if (preRollChunks.length > 5) preRollChunks.shift();
          }
        }
      };

      source.connect(scriptNode);
      scriptNode.connect(audioCtx.destination);
    } catch (err) {
      stopVoiceDictation();
      const msg = err && err.message ? err.message : String(err);
      addLog('ERR', `Mikrofon/Live: ${msg}`);
      showToast(`Start fehlgeschlagen: ${msg}`);
      setVoiceStatus('Fehler', true);
    }
  }

  function stopVoiceDictation() {
    const wasActive = voiceActive;
    voiceActive = false;

    if (wasActive && vadActiveChunks.length > 0) {
      flushCurrentVadSegment(true);
    }

    if (scriptNode) {
      try { scriptNode.disconnect(); } catch (_) {}
      scriptNode = null;
    }
    if (audioCtx) {
      try { audioCtx.close(); } catch (_) {}
      audioCtx = null;
    }
    if (mediaStream) {
      try { mediaStream.getTracks().forEach((t) => t.stop()); } catch (_) {}
      mediaStream = null;
    }
    if (liveWs) {
      try { liveWs.close(); } catch (_) {}
      liveWs = null;
    }

    const btn = $('btnVoiceToggle');
    if (btn) btn.classList.remove('active');
    const cp = $('voiceCockpit');
    if (cp) cp.classList.remove('recording');
    if ($('voiceMicIcon')) $('voiceMicIcon').textContent = '🎙️';
    if ($('voiceBtnLabel')) $('voiceBtnLabel').textContent = 'Live-Diktat starten';
    if ($('voiceLevelBar')) $('voiceLevelBar').style.width = '0%';
    if (pendingGeminiTasks === 0) setVoiceStatus('Bereit');
  }

  function updateNoiseGateUi() {
    const gatePct = Math.min(88, Math.max(8, parseInt($('rangeNoiseGate').value, 10) || 42));
    geminiCfg.noiseGatePct = gatePct;
    const marker = $('voiceGateMarker');
    if (marker) marker.style.left = `${gatePct}%`;

    let desc = 'Mittel';
    if (gatePct <= 22) desc = `Empfindlich (${gatePct}%)`;
    else if (gatePct <= 40) desc = `Normal (${gatePct}%)`;
    else if (gatePct <= 60) desc = `Streng (${gatePct}%)`;
    else desc = `Sehr streng (${gatePct}%)`;
    $('lblNoiseGate').textContent = `Noise-Gate: ${desc}`;
  }

  function syncGeminiCfgFromUi() {
    geminiCfg.apiKey = ($('inpGeminiKey').value || '').trim();
    geminiCfg.engine = $('selVoiceEngine').value || 'smart_vad';
    if ($('selGeminiModel').value) {
      geminiCfg.model = $('selGeminiModel').value;
    }
    geminiCfg.customModel = ($('inpCustomGeminiModel').value || '').trim();
    geminiCfg.style = $('selVoiceStyle').value || 'orig';
    geminiCfg.clean = $('chkVoiceClean').checked;
    geminiCfg.commands = $('chkVoiceCommands').checked;
    geminiCfg.autoSpace = $('chkVoiceAutoSpace').checked;
    geminiCfg.vadSilenceMs = parseInt($('rangeVadSilence').value, 10) || 800;
    updateNoiseGateUi();
    saveGeminiCfg();
  }

  function scoreModelForDictation(id) {
    const verMatch = id.match(/gemini-(\d+(?:\.\d+)?)/);
    const verNum = verMatch ? parseFloat(verMatch[1]) * 100 : 100;
    let bonus = 0;
    if (id.includes('flash') && !id.includes('lite')) bonus += 35;
    else if (id.includes('flash-lite') || id.includes('-lite')) bonus += 30;
    else if (id.includes('pro')) bonus += 15;
    if (id.includes('exp') || id.includes('customtools')) bonus -= 15;
    return verNum + bonus;
  }

  function populateModelDropdown(modelList, preferredSelectedId) {
    if (!Array.isArray(modelList) || modelList.length === 0) return;
    const sel = $('selGeminiModel');
    sel.innerHTML = '';
    modelList.forEach((m) => {
      const opt = document.createElement('option');
      opt.value = m.id;
      const tag = m.hasBidi ? ' [Live-WS + Stream]' : ' [Smart-Stream]';
      opt.textContent = `${m.id}${tag}`;
      sel.appendChild(opt);
    });
    const ids = modelList.map((x) => x.id);
    if (preferredSelectedId && ids.includes(preferredSelectedId)) {
      sel.value = preferredSelectedId;
    } else {
      sel.value = modelList[0].id;
    }
    geminiCfg.model = sel.value;
  }

  function applyGeminiCfgToUi() {
    $('inpGeminiKey').value = geminiCfg.apiKey || '';
    $('selVoiceEngine').value = geminiCfg.engine || 'smart_vad';

    if (Array.isArray(geminiCfg.cachedModels) && geminiCfg.cachedModels.length > 0) {
      populateModelDropdown(geminiCfg.cachedModels, geminiCfg.model);
    } else if (geminiCfg.model) {
      const sel = $('selGeminiModel');
      if (!Array.from(sel.options).some((o) => o.value === geminiCfg.model)) {
        const opt = document.createElement('option');
        opt.value = geminiCfg.model;
        opt.textContent = geminiCfg.model;
        sel.appendChild(opt);
      }
      sel.value = geminiCfg.model;
    }

    $('inpCustomGeminiModel').value = geminiCfg.customModel || '';
    $('selVoiceStyle').value = geminiCfg.style || 'orig';
    $('chkVoiceClean').checked = !!geminiCfg.clean;
    $('chkVoiceCommands').checked = !!geminiCfg.commands;
    $('chkVoiceAutoSpace').checked = !!geminiCfg.autoSpace;
    $('rangeVadSilence').value = String(geminiCfg.vadSilenceMs || 800);
    $('lblVadSilence').textContent = `Sprechpause: ${((geminiCfg.vadSilenceMs || 800) / 1000).toFixed(1)}s`;

    const sliderVal = Math.max(10, Math.min(85, Math.round(geminiCfg.noiseGatePct || 42)));
    $('rangeNoiseGate').value = String(sliderVal);
    updateNoiseGateUi();
    updateVoiceOutputModeUi();
    updateUsageStatsUi();

    if (geminiCfg.apiKey) {
      $('geminiKeyBadge').textContent = geminiCfg.cachedModels?.length
        ? `Aktiv ✓ (${geminiCfg.cachedModels.length} Modelle)`
        : 'Key gespeichert ✓';
      $('geminiKeyBadge').style.color = 'var(--primary)';
    }
  }

  function updateVoiceOutputModeUi() {
    const isLive = geminiCfg.outputMode === 'live';
    $('btnVoiceModeLive').classList.toggle('btn-primary', isLive);
    $('btnVoiceModePreview').classList.toggle('btn-primary', !isLive);
  }

  async function testKeyAndLoadModels(silent = false) {
    syncGeminiCfgFromUi();
    if (!geminiCfg.apiKey) {
      if (!silent) alert('Bitte gib zuerst deinen Google Gemini API-Key ein.');
      return;
    }
    if (!silent) haptic(15);
    $('geminiKeyBadge').textContent = 'Prüfe Key…';
    try {
      const resp = await fetch(
        `https://generativelanguage.googleapis.com/v1beta/models?key=${encodeURIComponent(geminiCfg.apiKey)}`
      );
      if (!resp.ok) {
        const err = await resp.json().catch(() => ({}));
        throw new Error((err.error && err.error.message) || `HTTP ${resp.status}`);
      }
      const data = await resp.json();
      const rawModels = (data.models || [])
        .filter((m) => Array.isArray(m.supportedGenerationMethods) &&
          (m.supportedGenerationMethods.includes('generateContent') ||
           m.supportedGenerationMethods.includes('bidiGenerateContent')))
        .map((m) => ({
          id: m.name.replace(/^models\//, ''),
          hasBidi: m.supportedGenerationMethods.includes('bidiGenerateContent'),
          hasGen: m.supportedGenerationMethods.includes('generateContent')
        }))
        .filter((m) =>
          m.id.startsWith('gemini-') &&
          !m.id.includes('-tts') &&
          !m.id.includes('image') &&
          !m.id.includes('embedding') &&
          !m.id.includes('vision') &&
          !m.id.includes('robotics') &&
          !m.id.includes('aqa')
        );

      if (rawModels.length > 0) {
        // Sortiere höchste Versionsnummern (3.8 / 3.5 Flash-Lite & Flash) automatisch ganz nach oben!
        rawModels.sort((a, b) => scoreModelForDictation(b.id) - scoreModelForDictation(a.id));
        geminiCfg.cachedModels = rawModels;
        populateModelDropdown(rawModels, geminiCfg.model);
        saveGeminiCfg();
      }
      $('geminiKeyBadge').textContent = `Aktiv ✓ (${rawModels.length} Modelle)`;
      $('geminiKeyBadge').style.color = 'var(--primary)';
      if (!silent) {
        showToast(`Geladen! Top-Modell: ${ $('selGeminiModel').value }`);
      }
    } catch (err) {
      $('geminiKeyBadge').textContent = 'Key ungültig';
      $('geminiKeyBadge').style.color = 'var(--danger)';
      if (!silent) alert(`Gemini API-Fehler: ${err.message || err}`);
    }
  }

  async function polishTextWithGemini() {
    syncGeminiCfgFromUi();
    const box = $('voiceTranscriptBox');
    const inputTxt = (box.value || '').trim();
    if (!inputTxt) {
      showToast('Bitte zuerst Text diktieren oder eingeben');
      return;
    }
    if (!geminiCfg.apiKey) {
      showToast('Bitte zuerst deinen Gemini API-Key hinterlegen');
      return;
    }
    haptic(15);
    setVoiceStatus('KI-Politur…');
    try {
      const model = getSelectedGeminiModel();
      const url = `https://generativelanguage.googleapis.com/v1beta/models/${encodeURIComponent(model)}:generateContent?key=${encodeURIComponent(geminiCfg.apiKey)}`;
      const resp = await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          systemInstruction: {
            parts: [{ text: buildSystemInstruction() }]
          },
          contents: [
            {
              role: 'user',
              parts: [{ text: `Überarbeite/übersetze folgenden Text gemäß System-Anweisung und gib NUR das Ergebnis aus:\n\n${inputTxt}` }]
            }
          ],
          generationConfig: { temperature: 0.2 }
        })
      });
      if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
      const data = await resp.json();
      const out = data.candidates?.[0]?.content?.parts?.map((p) => p.text || '').join('').trim();
      if (out) {
        box.value = out;
        showToast('Text mit Gemini überarbeitet ✨');
      }
    } catch (err) {
      showToast(`KI-Politur fehlgeschlagen: ${err.message || err}`);
    } finally {
      setVoiceStatus(voiceActive ? 'Hört zu…' : 'Bereit');
    }
  }

  function initVoiceDictation() {
    loadGeminiCfg();
    applyGeminiCfgToUi();

    if (geminiCfg.apiKey && (!geminiCfg.cachedModels || geminiCfg.cachedModels.length === 0)) {
      testKeyAndLoadModels(true);
    }

    $('btnVoiceModeLive').addEventListener('click', () => {
      haptic(12);
      geminiCfg.outputMode = 'live';
      saveGeminiCfg();
      updateVoiceOutputModeUi();
      showToast('Modus: ⚡ Tippt jeden Satz sofort live am PC');
    });

    $('btnVoiceModePreview').addEventListener('click', () => {
      haptic(12);
      geminiCfg.outputMode = 'preview';
      saveGeminiCfg();
      updateVoiceOutputModeUi();
      showToast('Modus: 📝 Sammelt Text erst im Vorschau-Feld');
    });

    $('btnVoiceToggle').addEventListener('click', () => {
      if (voiceActive) {
        haptic(20);
        stopVoiceDictation();
      } else {
        startVoiceDictation();
      }
    });

    $('btnVoiceFlushNow').addEventListener('click', () => {
      haptic(12);
      if (voiceActive) {
        flushCurrentVadSegment(true);
        showToast('Abschnitt sofort an Gemini gesendet');
      } else {
        showToast('Nur während laufendem Diktat aktiv');
      }
    });

    $('btnVoiceSendEnter').addEventListener('click', () => {
      haptic(14);
      sendCommand('K ENTER');
    });

    $('btnVoiceSendTab').addEventListener('click', () => {
      haptic(14);
      sendCommand('K TAB');
    });

    $('btnVoiceSendBs').addEventListener('click', () => {
      haptic(14);
      sendCommand('K BACKSPACE');
    });

    $('btnVoiceTypeAll').addEventListener('click', () => {
      const txt = $('voiceTranscriptBox').value;
      if (!txt) {
        showToast('Transkript-Feld ist leer');
        return;
      }
      typeLongText(txt, false, false, 'dongle');
    });

    $('btnVoiceAiPolish').addEventListener('click', polishTextWithGemini);

    $('btnVoiceToTextTab').addEventListener('click', () => {
      const txt = $('voiceTranscriptBox').value;
      if (!txt) {
        showToast('Transkript-Feld ist leer');
        return;
      }
      $('liveTextInput').value = txt;
      $('charCountBadge').textContent = `${txt.length} Zeichen`;
      document.querySelectorAll('.nav-item').forEach((b) => {
        b.classList.toggle('active', b.dataset.tab === 'tab-text');
      });
      document.querySelectorAll('.tab-panel').forEach((p) => {
        p.classList.toggle('active', p.id === 'tab-text');
      });
      showToast('In den Text-Tab übernommen');
    });

    $('btnVoiceClear').addEventListener('click', () => {
      $('voiceTranscriptBox').value = '';
      setVoiceInterim('Transkript geleert.');
    });

    $('voiceUsageStats').addEventListener('click', () => {
      if (confirm('Audio- & Token-Verbrauchszähler auf 0 zurücksetzen?')) {
        geminiCfg.totalAudioSec = 0;
        geminiCfg.totalTokens = 0;
        saveGeminiCfg();
        updateUsageStatsUi();
      }
    });

    $('btnToggleKeyVis').addEventListener('click', () => {
      const inp = $('inpGeminiKey');
      inp.type = inp.type === 'password' ? 'text' : 'password';
    });

    $('rangeNoiseGate').addEventListener('input', () => {
      updateNoiseGateUi();
      saveGeminiCfg();
    });

    $('rangeVadSilence').addEventListener('input', () => {
      const ms = parseInt($('rangeVadSilence').value, 10) || 800;
      $('lblVadSilence').textContent = `Sprechpause: ${(ms / 1000).toFixed(1)}s`;
      syncGeminiCfgFromUi();
    });

    $('selVoiceEngine').addEventListener('change', () => {
      if ($('selVoiceEngine').value === 'live_ws' && Array.isArray(geminiCfg.cachedModels)) {
        const curId = $('selGeminiModel').value;
        const curObj = geminiCfg.cachedModels.find((m) => m.id === curId);
        if (!curObj || !curObj.hasBidi) {
          const bestBidi = geminiCfg.cachedModels.find((m) => m.hasBidi);
          if (bestBidi) {
            $('selGeminiModel').value = bestBidi.id;
            showToast(`Für WebSocket kompatibles Modell gewählt: ${bestBidi.id}`);
          }
        }
      }
      syncGeminiCfgFromUi();
    });

    ['selVoiceStyle', 'chkVoiceClean', 'chkVoiceCommands', 'chkVoiceAutoSpace', 'selGeminiModel', 'inpCustomGeminiModel', 'inpGeminiKey'].forEach((id) => {
      const el = $(id);
      if (el) el.addEventListener('change', syncGeminiCfgFromUi);
    });

    $('btnSaveAndTestGemini').addEventListener('click', () => testKeyAndLoadModels(false));
  }

  // Register Service Worker for offline PWA support
  if ('serviceWorker' in navigator) {
    window.addEventListener('load', () => {
      navigator.serviceWorker.register('./sw.js').catch(() => {});
    });
  }

  renderSnippets();
  initTouchpad();
  initEvents();
  initVoiceDictation();
  updateConnectionUi();
})();
