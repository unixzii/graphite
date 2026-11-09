// DOM queries for tests/web.c, linked with --js-library.
addToLibrary({
  // The host element's layer tree as nesting depths, e.g. "0(1(2)(2))(1)": each layer is its depth followed by its
  // sublayers in parentheses.
  web_test_layer_tree__deps: ['$stringToNewUTF8'],
  web_test_layer_tree: () => {
    const walk = (el, depth) => {
      const subs = [...(el.querySelector(':scope > div > div > .gr-sublayers')?.children ?? [])];
      return depth + subs.map((s) => '(' + walk(s, depth + 1) + ')').join('');
    };
    const root = document.querySelector('#host > .gr-host')?.firstChild;
    return stringToNewUTF8(root ? walk(root, 0) : '');
  },

  web_test_host_count: () => document.querySelectorAll('.gr-host').length,

  // Registers a new element without an id or class under name, so only the registration can find it.
  web_test_register_element__deps: ['$specialHTMLTargets', '$UTF8ToString'],
  web_test_register_element: (name) => {
    const element = document.createElement('section');
    document.body.append(element);
    specialHTMLTargets[UTF8ToString(name)] = element;
  },

  // The number of host elements directly inside the element registered under name.
  web_test_hosts_in__deps: ['$specialHTMLTargets', '$UTF8ToString'],
  web_test_hosts_in: (name) => specialHTMLTargets[UTF8ToString(name)].querySelectorAll(':scope > .gr-host').length,

  // Synthetic input on the newest host element, at host coordinates; each returns whether the default was prevented.
  $webTestHost: () => [...document.querySelectorAll('.gr-host')].pop(),
  web_test_pointer__deps: ['$webTestHost', '$UTF8ToString'],
  web_test_pointer: (type, x, y, button, buttons, kind, pressure) => {
    const host = webTestHost(), r = host.getBoundingClientRect();
    const e = new PointerEvent(UTF8ToString(type), {
      clientX: r.left + x, clientY: r.top + y, button, buttons, pointerType: UTF8ToString(kind), pointerId: 7,
      pressure, bubbles: true, cancelable: true, composed: true,
    });
    host.dispatchEvent(e);
    return e.defaultPrevented;
  },

  web_test_wheel__deps: ['$webTestHost'],
  web_test_wheel: (x, y, dx, dy, mode) => {
    const host = webTestHost(), r = host.getBoundingClientRect();
    const e = new WheelEvent('wheel', {
      clientX: r.left + x, clientY: r.top + y, deltaX: dx, deltaY: dy, deltaMode: mode, bubbles: true, cancelable: true,
    });
    host.dispatchEvent(e);
    return e.defaultPrevented;
  },

  web_test_key__deps: ['$webTestHost', '$UTF8ToString'],
  web_test_key: (type, code, key, shift, meta, composing, repeat) => {
    const e = new KeyboardEvent(UTF8ToString(type), {
      code: UTF8ToString(code), key: UTF8ToString(key), shiftKey: !!shift, metaKey: !!meta, isComposing: !!composing,
      repeat: !!repeat, bubbles: true, cancelable: true,
    });
    webTestHost().dispatchEvent(e);
    return e.defaultPrevented;
  },

  web_test_focus__deps: ['$webTestHost'],
  web_test_focus: (focused) =>
    webTestHost().dispatchEvent(new FocusEvent(focused ? 'focusin' : 'focusout', { bubbles: true })),

  // The newest host's hidden text field as "tag type|value|selection start|end|left,top|focused", or "".
  $webTestField: () => webTestHost().querySelector('.gr-text-input'),
  web_test_field__deps: ['$webTestField', '$stringToNewUTF8'],
  web_test_field: () => {
    const f = webTestField();
    if (!f) return stringToNewUTF8('');
    const tag = f.tagName.toLowerCase() + (f.type === 'password' ? ' password' : '');
    return stringToNewUTF8(`${tag}|${f.value}|${f.selectionStart}|${f.selectionEnd}|${parseFloat(f.style.left)},` +
      `${parseFloat(f.style.top)}|${document.activeElement === f}`);
  },

  // What a browser does to the field when the user types text: replace the selection, then fire input.
  web_test_type__deps: ['$webTestField', '$UTF8ToString'],
  web_test_type: (text) => {
    const f = webTestField(), data = UTF8ToString(text);
    f.setRangeText(data, f.selectionStart, f.selectionEnd, 'end');
    f.dispatchEvent(new InputEvent('input', { inputType: 'insertText', data, bubbles: true }));
  },

  // What a browser does during an input method composition: start (over the selection), update (replacing the
  // composition so far, with the caret after it) and end, as compositionstart/update/end with input events.
  $webTestComposition: { start: 0, length: 0 },
  web_test_compose__deps: ['$webTestField', '$webTestComposition', '$UTF8ToString'],
  web_test_compose: (type, text) => {
    const f = webTestField(), c = webTestComposition, data = UTF8ToString(text), kind = UTF8ToString(type);
    if (kind === 'start') {
      c.start = f.selectionStart;
      c.length = f.selectionEnd - f.selectionStart;
      f.dispatchEvent(new CompositionEvent('compositionstart', { data: '', bubbles: true }));
      return;
    }
    f.dispatchEvent(new CompositionEvent(kind === 'end' ? 'compositionend' : 'compositionupdate', { data, bubbles: true }));
    if (kind === 'end') return;
    f.setRangeText(data, c.start, c.start + c.length, 'end');
    c.length = data.length;
    f.dispatchEvent(new InputEvent('input', { inputType: 'insertCompositionText', data, isComposing: true,
      bubbles: true }));
  },

  // A key event on the hidden field with modifiers (1 shift, 2 control, 4 alt, 8 meta); returns whether the default
  // was prevented.
  web_test_field_key__deps: ['$webTestField', '$UTF8ToString'],
  web_test_field_key: (code, key, modifiers) => {
    const e = new KeyboardEvent('keydown', {
      code: UTF8ToString(code), key: UTF8ToString(key), shiftKey: !!(modifiers & 1), ctrlKey: !!(modifiers & 2),
      altKey: !!(modifiers & 4), metaKey: !!(modifiers & 8), bubbles: true, cancelable: true,
    });
    webTestField().dispatchEvent(e);
    return e.defaultPrevented;
  },

  web_test_host_focused__deps: ['$webTestHost'],
  web_test_host_focused: () => webTestHost().contains(document.activeElement),

  // Whether editing keys follow macOS (Option for words, Command for lines) in this browser.
  web_test_apple: () => /Mac|iPhone|iPad|iPod/.test(navigator.platform),

  web_test_cursor__deps: ['$webTestHost', '$stringToNewUTF8'],
  web_test_cursor: () => stringToNewUTF8(webTestHost().style.cursor),

  // Asks the runner (tests/web/run.mjs) to change devicePixelRatio.
  web_test_set_scale: (scale) => {
    window.graphiteSetScale(scale);
  },

  // The text the newest host element shows.
  web_test_host_text__deps: ['$webTestHost', '$stringToNewUTF8'],
  web_test_host_text: () => stringToNewUTF8(webTestHost().textContent),
});
