(() => {
  'use strict';

  const SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
  const CHAR_RX_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';
  const CHAR_TX_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';
  const STORAGE_KEY = 'tdongle_k400_snippets_v1';

  let bleDevice = null;
  let rxChar = null;
  let txChar = null;
  let writePromise = Promise.resolve();
  let mouseBusy = false;
  let rxLineBuffer = '';

  let deviceState = {
    j: 1,
    min: 30,
    max: 60,
    mode: 0,
    auto: 1,
    lay: 'DE',
    delay: 10,
    stealth: 0,
    cnt: 0,
    next: 0,
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
      title: 'Git Status & Log',
      cat: 'Befehle',
      text: 'git status && git log -n 5 --oneline',
      enter: true,
      slot: 3
    },
    {
      id: 's5',
      title: 'PowerShell USB-Check',
      cat: 'Befehle',
      text: 'Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match "VID_046D" }',
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

  // --- BLE Communication ---
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
    updateStatusPills();
  }

  function updateStatusPills() {
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
    $('selJiggleMode').value = String(deviceState.mode);
    $('inpJiggleMin').value = String(deviceState.min);
    $('inpJiggleMax').value = String(deviceState.max);
    $('chkJiggleAuto').checked = !!deviceState.auto;
    $('selLayout').value = deviceState.lay;
    $('inpKeyDelay').value = String(deviceState.delay);
    $('inpBlePin').value = String(deviceState.pin);
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
    addLog('RX', line);
    if (line.startsWith('STAT ')) {
      try {
        const obj = JSON.parse(line.slice(5));
        deviceState = { ...deviceState, ...obj };
        updateConnectionUi();
      } catch (e) {
        console.warn('Bad STAT JSON:', e);
      }
      return;
    }
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
      await sendCommand('SNIP LIST');
    } catch (err) {
      addLog('ERR', err.message || String(err));
      showToast('Verbindung abgebrochen');
    }
  }

  function sendCommand(cmdLine, silent = false) {
    if (!isConnected()) {
      if (!silent) showToast('Nicht verbunden – bitte oben auf Verbinden tippen');
      return Promise.resolve(false);
    }

    if (!silent) addLog('TX', cmdLine);

    const payload = new TextEncoder().encode(cmdLine + '\n');
    const chunkSize = 180;

    writePromise = writePromise
      .then(async () => {
        for (let offset = 0; offset < payload.length; offset += chunkSize) {
          const slice = payload.slice(offset, offset + chunkSize);
          if (rxChar.properties && rxChar.properties.writeWithoutResponse && silent) {
            await rxChar.writeValueWithoutResponse(slice);
          } else {
            await rxChar.writeValue(slice);
          }
        }
        return true;
      })
      .catch((err) => {
        if (!silent) addLog('ERR', err.message || String(err));
        return false;
      });

    return writePromise;
  }

  // Sendet auch sehr lange Textmodule zuverlässig in Segmenten
  async function typeLongText(rawText, appendEnter = false, clearBefore = false) {
    if (!isConnected()) {
      showToast('Bitte zuerst mit dem Dongle verbinden!');
      return;
    }
    haptic(15);
    if (clearBefore) {
      await sendCommand('K STRG+A');
      await sendCommand('K BACKSPACE');
    }

    const fullText = appendEnter ? rawText + '\n' : rawText;
    const maxSegmentChars = 600;
    for (let i = 0; i < fullText.length; i += maxSegmentChars) {
      const seg = fullText.slice(i, i + maxSegmentChars);
      const escaped = escapeForWire(seg);
      await sendCommand(`T ${escaped}`);
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
        typeLongText(s.text, !!s.enter, false);
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

    // Header Status Pills
    $('pillJiggler').addEventListener('click', () => {
      haptic(15);
      sendCommand('J TOGGLE');
    });
    $('pillLayout').addEventListener('click', () => {
      haptic(15);
      const next = deviceState.lay === 'DE' ? 'US' : 'DE';
      sendCommand(`SYS LAYOUT ${next}`);
    });
    $('pillStealth').addEventListener('click', () => {
      haptic(15);
      const next = deviceState.stealth ? 0 : 1;
      sendCommand(`SYS STEALTH ${next}`);
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

    // Live Text Typer
    const liveInput = $('liveTextInput');
    liveInput.addEventListener('input', () => {
      $('charCountBadge').textContent = `${liveInput.value.length} Zeichen`;
    });

    $('btnSendLiveText').addEventListener('click', () => {
      const txt = liveInput.value;
      if (!txt) {
        showToast('Bitte zuerst Text eingeben');
        return;
      }
      typeLongText(txt, $('chkAppendEnter').checked, $('chkClearBefore').checked);
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
        await typeLongText(clip, $('chkAppendEnter').checked, $('chkClearBefore').checked);
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
    $('btnJigglerOn').addEventListener('click', () => { haptic(15); sendCommand('J ON'); });
    $('btnJigglerOff').addEventListener('click', () => { haptic(15); sendCommand('J OFF'); });

    $('btnSaveJiggleCfg').addEventListener('click', () => {
      const minS = parseInt($('inpJiggleMin').value, 10) || 30;
      const maxS = parseInt($('inpJiggleMax').value, 10) || 60;
      const mode = parseInt($('selJiggleMode').value, 10) || 0;
      const auto = $('chkJiggleAuto').checked ? 1 : 0;
      haptic(18);
      sendCommand(`JCFG ${minS} ${maxS} ${mode} ${auto}`);
      showToast('Jiggler-Konfiguration gespeichert');
    });

    $('selLayout').addEventListener('change', () => {
      sendCommand(`SYS LAYOUT ${$('selLayout').value}`);
    });

    $('inpKeyDelay').addEventListener('change', () => {
      const d = parseInt($('inpKeyDelay').value, 10) || 10;
      sendCommand(`SYS DELAY ${d}`);
    });

    $('btnSavePin').addEventListener('click', () => {
      const pin = parseInt($('inpBlePin').value, 10);
      if (isNaN(pin) || pin < 100000 || pin > 999999) {
        alert('Bitte eine 6-stellige PIN zwischen 100000 und 999999 eingeben.');
        return;
      }
      sendCommand(`SYS PIN ${pin}`);
      showToast(`Neue feste PIN: ${pin}`);
    });

    $('btnToggleStealth').addEventListener('click', () => {
      const next = deviceState.stealth ? 0 : 1;
      sendCommand(`SYS STEALTH ${next}`);
    });

    $('btnRequestStatus').addEventListener('click', () => sendCommand('SYS STATUS'));

    $('btnBootloader').addEventListener('click', () => {
      if (confirm('T-Dongle S3 jetzt in den USB-Flash-Modus (Bootloader) neustarten?\n\nDer Dongle meldet sich danach als COM-Port am PC an.')) {
        sendCommand('SYS BOOTLOADER');
      }
    });
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
  updateConnectionUi();
})();
