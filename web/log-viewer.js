// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
class LogViewer {
  constructor(id, controls, notify) {
    this.field = document.getElementById(id);
    this.controls = controls; this.notify = notify;
    this.buffer = ''; this.previous = []; this.locked = false; this.dragging = false;
    const byId = key => document.getElementById(controls[key]);
    this.lockButton = byId('lock'); this.status = byId('status');
    this.lockButton.onclick = () => this.setLocked(!this.locked);
    byId('copy').onclick = () => this.copy();
    byId('select').onclick = () => {
      this.setLocked(true); const top = this.field.scrollTop;
      this.field.focus({preventScroll:true}); this.field.select(); this.field.scrollTop = top;
    };
    byId('clear').onclick = () => { this.buffer = ''; this.field.value = ''; this.updateStatus(); };
    byId('download').onclick = () => this.download();
    byId('wrap').onchange = event => { this.field.wrap = event.target.checked ? 'soft':'off'; };
    this.field.addEventListener('pointerdown', () => { this.dragging = true; });
    const release = () => {
      if (!this.dragging) return;
      this.dragging = false;
      if (this.hasSelection() || !this.atBottom()) this.setLocked(true);
      else this.render();
    };
    window.addEventListener('pointerup', release);
    window.addEventListener('pointercancel', release);
    window.addEventListener('blur', release);
    this.field.addEventListener('select', () => { if (this.hasSelection()) this.setLocked(true); });
    this.field.addEventListener('scroll', () => { if (!this.locked && !this.atBottom()) this.setLocked(true); });
    this.updateStatus();
  }
  hasSelection() { return this.field.selectionStart !== this.field.selectionEnd; }
  atBottom() { return this.field.scrollHeight - this.field.clientHeight - this.field.scrollTop <= 4; }
  setLocked(locked) {
    this.locked = locked;
    this.lockButton.textContent = locked ? '🔒 Entsperren':'🔓 Sperren';
    this.lockButton.setAttribute('aria-pressed',String(locked));
    this.field.dataset.locked = String(locked);
    if (!locked) {
      this.field.setSelectionRange(this.field.value.length,this.field.value.length);
      this.render(); this.field.scrollTop = this.field.scrollHeight;
    }
    this.updateStatus();
  }
  append(chunk) {
    if (!chunk) return;
    this.buffer += chunk;
    const lines = this.buffer.split('\n');
    if (lines.length > 2001) this.buffer = lines.slice(-2001).join('\n');
    if (this.buffer.length > 262144) {
      this.buffer = this.buffer.slice(-262144);
      const newline = this.buffer.indexOf('\n');
      if (newline >= 0) this.buffer = this.buffer.slice(newline+1);
    }
    this.render();
  }
  snapshot(text) {
    // The ESP returns a rolling snapshot. Append only the non-overlapping tail,
    // retaining a longer browser history without duplicating the same lines.
    const incoming = text.replace(/\r\n/g,'\n').split('\n');
    if (incoming.at(-1) === '') incoming.pop();
    if (!incoming.length) return;
    let overlap = Math.min(this.previous.length,incoming.length);
    while (overlap > 0 && !this.previous.slice(-overlap).every((line,index) => line === incoming[index])) --overlap;
    this.previous = incoming;
    const added = incoming.slice(overlap);
    if (added.length) this.append(added.join('\n')+'\n');
    else this.updateStatus();
  }
  render() {
    if (!this.locked && !this.dragging && document.activeElement === this.field && this.hasSelection()) this.setLocked(true);
    if (!this.locked && !this.dragging && this.field.value !== this.buffer) {
      this.field.value = this.buffer;
      this.field.setSelectionRange(this.field.value.length,this.field.value.length);
      this.field.scrollTop = this.field.scrollHeight;
    }
    this.updateStatus();
  }
  updateStatus() {
    if (!this.lockButton.hasAttribute('aria-pressed')) {
      this.lockButton.textContent = '🔓 Sperren'; this.lockButton.setAttribute('aria-pressed','false');
    }
    const count = this.field.value ? this.field.value.trimEnd().split('\n').length : 0;
    this.status.textContent = this.locked
      ? `Ansicht gesperrt · ${count} Zeilen${this.buffer !== this.field.value ? ' · neue Daten warten':''}`
      : `Live · ${count} Zeilen · folgt automatisch`;
  }
  async copy() {
    const value = this.field.value;
    if (!value) return this.notify('Log ist leer.');
    try {
      if (window.isSecureContext && navigator.clipboard?.writeText) {
        try { await navigator.clipboard.writeText(value); this.notify('Log kopiert.'); return; } catch (_) { /* HTTP/permissions fallback below */ }
      }
      const active = document.activeElement;
      const start = this.field.selectionStart, end = this.field.selectionEnd, top = this.field.scrollTop;
      const helper = document.createElement('textarea');
      helper.className = 'clipboard-helper'; helper.readOnly = true; helper.value = value;
      document.body.append(helper); helper.select();
      let copied = false;
      try { copied = document.execCommand('copy'); }
      finally {
        helper.remove(); active?.focus({preventScroll:true});
        this.field.setSelectionRange(start,end); this.field.scrollTop = top;
      }
      if (!copied) throw new Error('Copy unavailable');
      this.notify('Log kopiert.');
    } catch (_) {
      this.setLocked(true); this.field.focus({preventScroll:true}); this.field.select();
      this.notify('Log markiert. Mit Strg+C oder der Kopieren-Funktion des Geräts kopieren.');
    }
  }
  download() {
    if (!this.field.value) return this.notify('Log ist leer.');
    const url = URL.createObjectURL(new Blob([this.field.value],{type:'text/plain;charset=utf-8'}));
    const link = document.createElement('a'); link.href = url; link.download = this.controls.filename;
    link.click(); setTimeout(()=>URL.revokeObjectURL(url),1000);
  }
}
