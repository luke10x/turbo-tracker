const term = new Terminal({
  cursorBlink: false,
  scrollback: 0,
  fontFamily: 'Menlo, Consolas, monospace',
  fontSize: 14,
  theme: { background: '#101010', foreground: '#e5e5e5',
    blue: '#07529f', green: '#149414', cyan: '#13b5c8',
    yellow: '#e8d327', red: '#d65b68', magenta: '#bd53c3', white: '#d5d5d5' }
});
const fit = new FitAddon.FitAddon();
term.loadAddon(fit);
term.open(document.getElementById('terminal'));
fit.fit();
const touchKeyboard = navigator.maxTouchPoints > 0;
let fullViewportHeight = window.visualViewport?.height || window.innerHeight;
let fullViewportWidth = window.visualViewport?.width || window.innerWidth;
let refocusingInput = false;
let editingCommitted = false;
let sawKeyboardViewport = false;
let keyboardDismissed = false;
if (touchKeyboard) {
  term.textarea.readOnly = true;
  term.textarea.inputMode = 'none';
} else term.focus();

function inputAt(x, y) {
  const editableRects = [...(Module.f80InputRects || [])];
  const grid = window.f80ApplicationId === 'turbo-tracker' ?
    Module.f80EditableGridRect : Module.f80GridRect;
  if (grid) editableRects.push(grid);
  if (window.f80ApplicationId === 'turbo-tracker' && Module.f80TableRect)
    editableRects.push(Module.f80TableRect);
  if (Module.f80ComponentGridRect) editableRects.push(Module.f80ComponentGridRect);
  return editableRects.some(rect =>
    x >= rect.x && x < rect.x + rect.width &&
    y >= rect.y && y < rect.y + rect.height);
}

function focusInputAt(x, y) {
  if (!touchKeyboard) return;
  const editing = inputAt(x, y);
  refocusingInput = true;
  // iOS can leave xterm's textarea focused after its keyboard is dismissed.
  // Blur the DOM element itself before every new focus request.
  term.textarea.blur();
  term.textarea.readOnly = !editing;
  term.textarea.inputMode = editing ? 'text' : 'none';
  if (editing) term.textarea.focus({ preventScroll: true });
  editingCommitted = editing;
  sawKeyboardViewport = false;
  keyboardDismissed = false;
  refocusingInput = false;
  resized();
}

window.addEventListener('message', event => {
  if (event.source !== window.parent || event.origin !== location.origin ||
      event.data?.type !== 'f80-terminal-settings') return;
  const { fontFamily, fontSize, lineHeight } = event.data;
  if (typeof fontFamily === 'string' && fontFamily.length <= 160) term.options.fontFamily = fontFamily;
  if (Number.isFinite(fontSize) && fontSize >= 8 && fontSize <= 32) term.options.fontSize = fontSize;
  if (Number.isFinite(lineHeight) && lineHeight >= 1 && lineHeight <= 2) term.options.lineHeight = lineHeight;
  fit.fit();
  if (ready) call('curses_push_resize', []);
});

window.Module = window.Module || {};
Module.term = term;
let ready = false;
const queued = [];
function call(name, args) {
  if (!ready) { queued.push([name, args]); return; }
  Module.ccall(name, null, args.map(() => 'number'), args);
}
function keyCode(event) {
  if (event.ctrlKey && !event.altKey && !event.metaKey && event.key.toLowerCase() === 'o') return 15;
  if (event.ctrlKey && !event.altKey && !event.metaKey && event.key.toLowerCase() === 'z') return 26;
  if (event.ctrlKey && !event.altKey && !event.metaKey && event.key.toLowerCase() === 'y') return 25;
  if (event.key === 'ArrowLeft' && event.ctrlKey) return 1012;
  if (event.key === 'ArrowRight' && event.ctrlKey) return 1013;
  if (event.key === 'ArrowLeft' && event.shiftKey) return 1014;
  if (event.key === 'ArrowRight' && event.shiftKey) return 1015;
  if (event.key === 'ArrowUp' && event.shiftKey) return 1016;
  if (event.key === 'ArrowDown' && event.shiftKey) return 1017;
  const special = { ArrowUp: 1001, ArrowDown: 1002, ArrowLeft: 1003,
    ArrowRight: 1004, Home: 1005, End: 1006, PageUp: 1007,
    PageDown: 1008, Backspace: 1009, Delete: 1010, Escape: 27,
    Enter: 10, Tab: event.shiftKey ? 1011 : 9 };
  if (event.key in special) return special[event.key];
  const f = /^F(\d+)$/.exec(event.key);
  if (f) return 2000 + Number(f[1]);
  return event.key.length === 1 ? event.key.charCodeAt(0) : -1;
}
term.onKey(({ domEvent }) => {
  const code = keyCode(domEvent);
  if (code >= 0) {
    call('curses_push_key', [code]);
    if (ready && window.f80ApplicationId === 'turbo-tracker')
      call('tt_tracker_flush_input', []);
  }
  domEvent.preventDefault();
});
function cell(event) {
  const rect = term.element.getBoundingClientRect();
  return [Math.max(0, Math.min(term.cols - 1,
    Math.floor((event.clientX - rect.left) * term.cols / rect.width))),
    Math.max(0, Math.min(term.rows - 1,
      Math.floor((event.clientY - rect.top) * term.rows / rect.height)))];
}
const activeContacts = new Map();
term.textarea.addEventListener('blur', () => {
  if (!refocusingInput && !activeContacts.size) {
    editingCommitted = false;
    resized();
  }
});
function sampleGridAt(x, y) {
  const r = Module.f80GridRect;
  return r && x >= r.x && x < r.x + r.width &&
    y >= r.y && y < r.y + r.height;
}
function sampleEditableGridAt(x, y) {
  const r = Module.f80EditableGridRect;
  return r && x >= r.x && x < r.x + r.width &&
    y >= r.y && y < r.y + r.height;
}
function sampleWaveAt(x, y) {
  const r = Module.f80WaveRect;
  return r && x >= r.x && x < r.x + r.width &&
    y >= r.y && y < r.y + r.height;
}
function sampleOrderAt(x, y) {
  const r = Module.f80OrderRect;
  return r && x >= r.x && x < r.x + r.width &&
    y >= r.y && y < r.y + r.height;
}
function sampleTableAt(x, y) {
  const r = Module.f80TableRect;
  return r && x >= r.x && x < r.x + r.width &&
    y >= r.y && y < r.y + r.height;
}
function componentGridAt(x, y) {
  const r = Module.f80ComponentGridRect;
  return r && x >= r.x && x < r.x + r.width &&
    y >= r.y && y < r.y + r.height;
}
function wrapAt(x, y) {
  const r = Module.f80WrapRect;
  return r && x >= r.x && x < r.x + r.width &&
    y >= r.y && y < r.y + r.height;
}
function moveTouch(start, x, y) {
  if (!start.target || !ready) return;
  if (start.target === 'component-grid') {
    if (start.holdStarted)
      Module.ccall('example_grid_drag', null, ['number', 'number'], [x, y]);
    else if (x !== start.lastX || y !== start.lastY)
      Module.ccall('example_grid_scroll', null, ['number', 'number'],
        [start.lastX - x, start.lastY - y]);
  } else if (start.target === 'wrap') {
    if (y !== start.lastY)
      Module.ccall(window.f80ApplicationId === 'turbo-tracker' ?
        'tt_tracker_scroll_table_actions' : 'example_wrap_scroll',
        null, ['number'], [start.lastY - y]);
  } else if (start.target === 'table') {
    if (start.holdStarted)
      Module.ccall('tt_tracker_drag_table_selection', null,
        ['number', 'number'], [x, y]);
    else {
      if (y !== start.lastY)
        Module.ccall('tt_tracker_scroll_table', null,
          ['number'], [start.lastY - y]);
      if (x !== start.lastX)
        Module.ccall('tt_tracker_scroll_table_horizontal', null,
          ['number'], [start.lastX - x]);
    }
  } else if (start.target === 'order') {
    if (start.holdStarted)
      Module.ccall('tt_tracker_drag_order_selection', null,
        ['number', 'number'], [x, y]);
    else if (y !== start.lastY)
      Module.ccall('tt_tracker_scroll_order', null,
        ['number'], [start.lastY - y]);
  } else if (start.target === 'grid') {
    if (start.holdStarted) {
      Module.ccall('tt_tracker_drag_selection', null,
        ['number', 'number'], [x, y]);
    } else {
      if (y !== start.lastY)
        Module.ccall('tt_tracker_touch_scroll', null,
          ['number'], [start.lastY - y]);
      if (x !== start.lastX)
        Module.ccall('tt_tracker_touch_scroll_fields', null,
          ['number'], [start.lastX - x]);
    }
  } else if (x !== start.lastX) {
    Module.ccall(start.holdStarted ? 'tt_tracker_move_wave_marker' :
      'tt_tracker_pan_wave_touch', null, ['number'], [x]);
  }
  start.lastX = x;
  start.lastY = y;
}
function beginContact(id, x, y) {
  const sample = ready && window.f80ApplicationId === 'turbo-tracker';
  const gridDemo = ready && (window.f80ApplicationId === 'examples/components/grid' ||
    window.f80ApplicationId === 'examples/components/auto');
  const wrapDemo = ready && window.f80ApplicationId === 'examples/components/wrap';
  const target = gridDemo && componentGridAt(x, y) ? 'component-grid' :
    (wrapDemo || sample) && wrapAt(x, y) ? 'wrap' : sample && (sampleTableAt(x, y) ? 'table' :
    sampleOrderAt(x, y) ? 'order' :
    sampleGridAt(x, y) ? 'grid' :
    sampleWaveAt(x, y) ? 'wave' : null);
  const start = { x, y, currentX: x, currentY: y,
    lastX: x, lastY: y, tapMoved: false, swiping: false,
    holdStarted: false, target };
  activeContacts.set(id, start);
  if (target === 'wave')
    Module.ccall('tt_tracker_begin_wave_gesture', null, ['number'], [x]);
  const tableRect = Module.f80TableRect;
  const tableCell = target === 'table' && tableRect &&
    x < tableRect.x + tableRect.width - (tableRect.vertical ? 1 : 0) &&
    y > tableRect.y && y < tableRect.y + tableRect.height - (tableRect.horizontal ? 1 : 0);
  const componentRect = Module.f80ComponentGridRect;
  const componentCell = target === 'component-grid' && componentRect &&
    x < componentRect.x + componentRect.width - (componentRect.vertical ? 1 : 0) &&
    y > componentRect.y && y < componentRect.y + componentRect.height - (componentRect.horizontal ? 1 : 0);
  if (target === 'wave' || target === 'order' ||
      target === 'grid' && sampleEditableGridAt(x, y) ||
      tableCell || componentCell) {
    start.holdTimer = setTimeout(() => {
      if (activeContacts.get(id) !== start || start.swiping) return;
      if (target === 'component-grid')
        Module.ccall('example_grid_hold', null,
          ['number', 'number'], [start.currentX, start.currentY]);
      else if (target === 'table')
        Module.ccall('tt_tracker_hold_table_selection', null,
          ['number', 'number'], [start.currentX, start.currentY]);
      else if (target === 'grid')
        Module.ccall('tt_tracker_begin_touch_selection', null,
          ['number', 'number'], [start.currentX, start.currentY]);
      else if (target === 'order')
        Module.ccall('tt_tracker_hold_order_selection', null,
          ['number', 'number'], [start.currentX, start.currentY]);
      else {
        Module.ccall('tt_tracker_begin_wave_gesture', null,
          ['number'], [start.currentX]);
        Module.ccall('tt_tracker_hold_wave_marker', null,
          ['number'], [start.currentX]);
      }
      start.lastX = start.currentX;
      start.lastY = start.currentY;
      start.holdStarted = true;
    }, 1000);
  }
  if (touchKeyboard) {
    term.textarea.blur();
    term.textarea.readOnly = true;
    term.textarea.inputMode = 'none';
  }
}
function updateContact(start, x, y) {
  start.currentX = x;
  start.currentY = y;
  if (x !== start.x || y !== start.y) start.tapMoved = true;
  if (x !== start.x || y !== start.y) {
    start.swiping = true;
    clearTimeout(start.holdTimer);
  }
}
function moveContact(id, x, y) {
  const start = activeContacts.get(id);
  if (!start) return;
  updateContact(start, x, y);
  if (start.swiping || start.holdStarted) moveTouch(start, x, y);
}
function endContact(id, x, y, cancelled) {
  const start = activeContacts.get(id);
  if (!start) return;
  activeContacts.delete(id);
  clearTimeout(start.holdTimer);
  if (cancelled) {
    editingCommitted = false;
    if (touchKeyboard) resized();
    return;
  }
  updateContact(start, x, y);
  if (start.tapMoved || start.holdStarted) {
    editingCommitted = false;
    moveTouch(start, x, y);
  }
  if (!start.tapMoved && !start.holdStarted) {
    call('curses_push_mouse', [x, y, 0]);
    if (ready && window.f80ApplicationId === 'turbo-tracker')
      call('tt_tracker_flush_input', []);
    focusInputAt(x, y);
    if (!touchKeyboard) term.focus();
  }
  if (touchKeyboard) resized();
}

const pointerGestures = 'PointerEvent' in window;
term.element.addEventListener('pointerdown', event => {
  if (pointerGestures && (touchKeyboard ||
      window.f80ApplicationId === 'turbo-tracker' ||
      event.pointerType === 'touch')) {
    if (event.pointerType === 'mouse' && event.button !== 0) return;
    const [x, y] = cell(event);
    beginContact(event.pointerId, x, y);
    term.element.setPointerCapture?.(event.pointerId);
    event.preventDefault();
    return;
  }
  if (event.pointerType === 'touch') return;
  const [x, y] = cell(event);
  call('curses_push_mouse', [x, y, 0]);
  if (ready && window.f80ApplicationId === 'turbo-tracker')
    call('tt_tracker_flush_input', []);
  term.focus();
  event.preventDefault();
});
term.element.addEventListener('pointermove', event => {
  if (pointerGestures && activeContacts.has(event.pointerId)) {
    const [x, y] = cell(event);
    moveContact(event.pointerId, x, y);
    event.preventDefault();
    return;
  }
  if (pointerGestures && window.f80ApplicationId === 'turbo-tracker') return;
  if (event.pointerType === 'touch') return;
  if (!(event.buttons & 1) || !ready ||
      window.f80ApplicationId !== 'turbo-tracker') return;
  const [x, y] = cell(event);
  Module.ccall('tt_tracker_drag_selection', null, ['number', 'number'], [x, y]);
  event.preventDefault();
});
for (const type of ['pointerup', 'pointercancel']) {
  term.element.addEventListener(type, event => {
    if (!activeContacts.has(event.pointerId)) return;
    const [x, y] = cell(event);
    endContact(event.pointerId, x, y, type === 'pointercancel');
    event.preventDefault();
  });
}
if (!pointerGestures) for (const type of ['touchstart', 'touchmove', 'touchend', 'touchcancel']) {
  term.element.addEventListener(type, event => {
    for (const touch of event.changedTouches) {
      const [x, y] = cell(touch);
      if (type === 'touchstart') beginContact(touch.identifier, x, y);
      else if (type === 'touchmove') moveContact(touch.identifier, x, y);
      else endContact(touch.identifier, x, y, type === 'touchcancel');
    }
    event.preventDefault();
  }, { passive: false });
}
term.element.addEventListener('wheel', event => {
  call('curses_push_mouse', [...cell(event), Math.sign(event.deltaY)]);
  event.preventDefault();
}, { passive: false });
let fittedCols = 0;
let fittedRows = 0;
function resized() {
  const viewport = window.visualViewport;
  if (touchKeyboard) {
    const visibleHeight = Math.max(1, viewport?.height || window.innerHeight);
    const visibleWidth = viewport?.width || window.innerWidth;
    if (visibleWidth !== fullViewportWidth) {
      fullViewportWidth = visibleWidth;
      fullViewportHeight = visibleHeight;
    }
    const editing = editingCommitted && document.activeElement === term.textarea;
    const viewportShrunk = fullViewportHeight - visibleHeight > 120;
    if (editing && viewportShrunk) sawKeyboardViewport = true;
    if (editing && sawKeyboardViewport && !viewportShrunk) keyboardDismissed = true;
    if (!editing) {
      fullViewportHeight = Math.max(fullViewportHeight, visibleHeight);
      sawKeyboardViewport = false;
      keyboardDismissed = false;
    }
    const compact = editing && !keyboardDismissed;
    // In some iPhone hosts the keyboard overlays the page without changing
    // visualViewport. Reserve the upper part of the screen while editing.
    const height = compact && !viewportShrunk ?
      Math.min(visibleHeight, Math.floor(fullViewportHeight * 0.55)) : visibleHeight;
    document.body.style.height = `${Math.round(height)}px`;
    document.body.style.transform = `translateY(${Math.round(viewport?.offsetTop || 0)}px)`;
    window.f80CompactEditing = compact;
  }
  fit.fit();
  if (term.cols !== fittedCols || term.rows !== fittedRows) {
    fittedCols = term.cols;
    fittedRows = term.rows;
    call('curses_push_resize', []);
  }
}
new ResizeObserver(resized).observe(document.getElementById('terminal'));
function viewportChanged() {
  resized();
  // WebKit can fire resize before visualViewport.height has its final value.
  requestAnimationFrame(resized);
}
window.addEventListener('resize', viewportChanged);
window.visualViewport?.addEventListener('resize', viewportChanged);
window.visualViewport?.addEventListener('scroll', viewportChanged);
window.addEventListener('blur', () => {
  if (touchKeyboard && document.activeElement === term.textarea) {
    keyboardDismissed = true;
    resized();
  }
});
Module.onRuntimeInitialized = () => {
  ready = true;
  for (const [name, args] of queued.splice(0)) call(name, args);
  resized();
};
