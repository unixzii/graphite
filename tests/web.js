// DOM queries for tests/web.c, linked with --js-library.
addToLibrary({
  // The host element's layer tree as nesting depths, e.g. "0(1(2)(2))(1)": each layer is its depth followed by its
  // sublayers in parentheses.
  web_test_layer_tree__deps: ['$stringToNewUTF8'],
  web_test_layer_tree: () => {
    const walk = (el, depth) => {
      const subs = [...(el.querySelector(':scope > div > .gr-sublayers')?.children ?? [])];
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
  web_test_focus: (focused) => webTestHost().dispatchEvent(new FocusEvent(focused ? 'focus' : 'blur')),

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
