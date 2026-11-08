// Graphite Web runtime: the DOM side of the Web backend, linked with --js-library.
// backend.c sends each commit as a stream of doubles; internal.h documents the commands and operand groups.

addToLibrary({
  $GRWeb__deps: ['$GRText'],
  $GRWeb: {
    hosts: new Map(),
    nextHost: 1,
    nextId: 1,
    /* Converted images by identity: a source canvas holding straight-alpha pixels, and how many uses hold it. */
    images: new Map(),
    SVG: 'http://www.w3.org/2000/svg',

    /* A realized layer:
     *   el            transform, opacity, blur
     *   ├ shadow      an svg, only while the layer has a shadow; outside the clip and mask
     *   └ group       the mask
     *     ├ body      the masks_to_bounds clip
     *     │ ├ background an svg, only while the layer has one
     *     │ ├ content the image, shape, gradient or drawing, when there is one
     *     │ └ sublayers, in local space (translated by the bounds offset)
     *     └ border    an svg, only while the layer has one; above contents and sublayers
     * Everything painted is SVG geometry or transformed, never CSS box sizes: browsers store those in fixed point
     * (1/64 px in Chromium), which puts device-aligned edges off the grid at fractional scales. */
    node(id) {
      const el = GRWeb.box('div');
      el.className = 'gr-layer';
      el.style.transformOrigin = '0 0';
      el.grId = id;
      const group = GRWeb.box('div');
      const body = GRWeb.box('div');
      const sublayers = GRWeb.box('div');
      sublayers.className = 'gr-sublayers';
      body.append(sublayers);
      group.append(body);
      el.append(group);
      return {
        el, group, body, sublayers, shadow: null, background: null, border: null, content: null, image: 0,
        size: [0, 0], offset: [0, 0], radius: 0, corners: 15, fill: '', stroke: null, masks: false,
      };
    },

    /* SVG path data for a rectangle with corner radii [top-left, top-right, bottom-right, bottom-left], each limited
     * to half the shorter side as Core Animation does. */
    roundedRect(x, y, w, h, radii) {
      const [tl, tr, br, bl] = radii.map((r) => Math.max(0, Math.min(r, w / 2, h / 2)));
      return `M${x + tl} ${y}H${x + w - tr}A${tr} ${tr} 0 0 1 ${x + w} ${y + tr}V${y + h - br}` +
        `A${br} ${br} 0 0 1 ${x + w - br} ${y + h}H${x + bl}A${bl} ${bl} 0 0 1 ${x} ${y + h - bl}V${y + tl}` +
        `A${tl} ${tl} 0 0 1 ${x + tl} ${y}Z`;
    },

    /* Repaints the background, border and clip from the node's size and appearance. */
    decorate(node) {
      const [w, h] = node.size;
      // Graphite's corner flags: top-left 1, top-right 2, bottom-left 4, bottom-right 8.
      const radii = [1, 2, 8, 4].map((flag) => (node.corners & flag ? node.radius : 0));
      const outline = GRWeb.roundedRect(0, 0, w, h, radii);
      node.body.style.clipPath = node.masks ? `path('${outline}')` : '';
      node.background?.remove();
      node.background = null;
      if (node.fill) {
        node.background = GRWeb.canvasSvg();
        GRWeb.svg(node.background, 'path', { d: outline, fill: node.fill });
        node.body.prepend(node.background);
      }
      node.border?.remove();
      node.border = null;
      if (node.stroke) {
        const [bw, color] = node.stroke;
        const inner = GRWeb.roundedRect(bw, bw, Math.max(0, w - 2 * bw), Math.max(0, h - 2 * bw), radii.map((r) => r - bw));
        node.border = GRWeb.canvasSvg();
        GRWeb.svg(node.border, 'path', { d: outline + inner, fill: color, 'fill-rule': 'evenodd' });
        node.group.append(node.border);
      }
    },

    /* An absolutely positioned element at the parent's origin. */
    box(tag) {
      const e = tag === 'svg' ? document.createElementNS(GRWeb.SVG, 'svg') : document.createElement(tag);
      Object.assign(e.style, { position: 'absolute', left: '0', top: '0' });
      return e;
    },

    svg(parent, name, attrs) {
      const e = document.createElementNS(GRWeb.SVG, name);
      for (const k in attrs) if (attrs[k] !== undefined && attrs[k] !== null) e.setAttribute(k, attrs[k]);
      parent?.append(e);
      return e;
    },

    color(r, g, b, a) {
      return `rgb(${r * 255} ${g * 255} ${b * 255} / ${a})`;
    },

    /* Operand readers over one command stream. */
    reader(words) {
      let i = 0;
      const R = {
        get done() { return i >= words.length; },
        next: () => words[i++],
        color: () => GRWeb.color(words[i++], words[i++], words[i++], words[i++]),
        rect: () => [words[i++], words[i++], words[i++], words[i++]],
        path() {
          const n = words[i++];
          const parts = [];
          for (let k = 0; k < n; k++) {
            switch (words[i++]) {
              case 0: parts.push(`M${words[i++]} ${words[i++]}`); break;
              case 1: parts.push(`L${words[i++]} ${words[i++]}`); break;
              case 2: parts.push(`Q${words[i++]} ${words[i++]} ${words[i++]} ${words[i++]}`); break;
              case 3: parts.push(`C${words[i++]} ${words[i++]} ${words[i++]} ${words[i++]} ${words[i++]} ${words[i++]}`); break;
              case 4: parts.push('Z'); break;
            }
          }
          return parts.join('');
        },
        /* A paint as data; GRWeb.fill turns it into an SVG paint. */
        paint() {
          const kind = words[i++];
          if (kind === 0) return null;
          if (kind === 1) return { kind, color: R.color() };
          const geometry = kind === 2 ? [words[i++], words[i++], words[i++], words[i++]] : [words[i++], words[i++], words[i++]];
          const stops = [];
          for (let n = words[i++]; n > 0; n--) stops.push({ offset: words[i++], color: [words[i++], words[i++], words[i++], words[i++]] });
          return { kind, geometry, stops };
        },
        stroke() {
          if (!words[i++]) return null;
          const s = { width: words[i++], cap: words[i++], join: words[i++], miter: words[i++], dash: [] };
          for (let n = words[i++]; n > 0; n--) s.dash.push(words[i++]);
          s.phase = words[i++];
          s.paint = R.paint();
          return s;
        },
        image() {
          return { id: words[i++], width: words[i++], height: words[i++], pixels: words[i++] };
        },
      };
      return R;
    },

    /* An SVG paint value for paint, adding gradient definitions to svg. units is 'userSpaceOnUse' or
     * 'objectBoundingBox'. */
    fill(svg, paint, units = 'userSpaceOnUse') {
      if (!paint) return 'none';
      if (paint.kind === 1) return paint.color;
      const id = 'gr' + GRWeb.nextId++;
      const [a, b, c, d] = paint.geometry;
      const g = paint.kind === 2
        ? GRWeb.svg(svg, 'linearGradient', { id, gradientUnits: units, x1: a, y1: b, x2: c, y2: d })
        : GRWeb.svg(svg, 'radialGradient', { id, gradientUnits: units, cx: a, cy: b, r: c });
      for (const s of paint.stops) {
        const [r, gr, bl, al] = s.color;
        GRWeb.svg(g, 'stop', { offset: s.offset, 'stop-color': GRWeb.color(r, gr, bl, 1), 'stop-opacity': al });
      }
      return `url(#${id})`;
    },

    strokeAttributes(svg, s) {
      return {
        fill: 'none', stroke: GRWeb.fill(svg, s.paint), 'stroke-width': s.width,
        'stroke-linecap': ['butt', 'round', 'square'][s.cap], 'stroke-linejoin': ['miter', 'round', 'bevel'][s.join],
        'stroke-miterlimit': s.miter, 'stroke-dasharray': s.dash.length ? s.dash.join(' ') : undefined,
        'stroke-dashoffset': s.dash.length ? s.phase : undefined,
      };
    },

    /* An svg drawing paths in its parent's coordinates: no box of its own, painting outside it. */
    canvasSvg() {
      const s = GRWeb.box('svg');
      s.setAttribute('width', 1);
      s.setAttribute('height', 1);
      s.style.overflow = 'visible';
      return s;
    },

    acquireImage(image) {
      let entry = GRWeb.images.get(image.id);
      if (!entry) {
        const canvas = document.createElement('canvas');
        canvas.width = image.width;
        canvas.height = image.height;
        const data = new ImageData(image.width, image.height);
        const src = HEAPU8.subarray(image.pixels, image.pixels + image.width * image.height * 4);
        const dst = data.data;
        // ImageData is straight alpha; Graphite images are premultiplied.
        for (let p = 0; p < dst.length; p += 4) {
          const a = src[p + 3];
          dst[p + 3] = a;
          if (a === 255) {
            dst[p] = src[p]; dst[p + 1] = src[p + 1]; dst[p + 2] = src[p + 2];
          } else if (a) {
            dst[p] = Math.min(255, Math.round((src[p] * 255) / a));
            dst[p + 1] = Math.min(255, Math.round((src[p + 1] * 255) / a));
            dst[p + 2] = Math.min(255, Math.round((src[p + 2] * 255) / a));
          }
        }
        canvas.getContext('2d').putImageData(data, 0, 0);
        entry = { canvas, uses: 0 };
        GRWeb.images.set(image.id, entry);
      }
      entry.uses++;
      return entry.canvas;
    },

    releaseImage(id) {
      const entry = GRWeb.images.get(id);
      if (entry && --entry.uses === 0) GRWeb.images.delete(id);
    },

    /* A canvas element showing image at rect [x, y, width, height], one CSS pixel per image pixel scaled by a
     * transform. */
    imageElement(source, rect) {
      const c = GRWeb.box('canvas');
      c.width = source.width;
      c.height = source.height;
      c.getContext('2d').drawImage(source, 0, 0);
      const [x, y, w, h] = rect;
      Object.assign(c.style, {
        width: source.width + 'px', height: source.height + 'px', transformOrigin: '0 0',
        transform: `matrix(${w / source.width}, 0, 0, ${h / source.height}, ${x}, ${y})`,
      });
      return c;
    },

    setContent(node, element) {
      node.content?.remove();
      node.content = element;
      if (element) node.body.insertBefore(element, node.sublayers);
    },

    /* Replays draw ops into a new element in local space. Saves, transforms and clips open wrapper divs; paths go
     * into svgs, consecutive paths sharing one; drawing alpha multiplies into each drawn element's opacity. */
    drawing(R, node) {
      const root = GRWeb.box('div');
      root.grClip = true;
      root.style.clipPath = `path('M0 0H${node.size[0]}V${node.size[1]}H0Z')`;
      const local = GRWeb.box('div');
      local.style.transform = `translate(${node.offset[0]}px, ${node.offset[1]}px)`;
      root.append(local);
      root.grLocal = local;
      let group = local, alpha = 1, svg = null;
      const stack = [];
      const wrap = (style) => {
        const d = GRWeb.box('div');
        Object.assign(d.style, style);
        group.append(d);
        group = d;
        svg = null;
      };
      const target = () => {
        if (!svg) {
          svg = GRWeb.canvasSvg();
          group.append(svg);
        }
        return svg;
      };
      const opacity = () => (alpha < 1 ? alpha : undefined);
      for (let op = R.next(); op !== -1; op = R.next()) {
        switch (op) {
          case 0: stack.push({ group, alpha }); svg = null; break; // SAVE
          case 1: ({ group, alpha } = stack.pop()); svg = null; break; // RESTORE
          case 2: { // TRANSFORM
            const m = [R.next(), R.next(), R.next(), R.next(), R.next(), R.next()];
            wrap({ transformOrigin: '0 0', transform: `matrix(${m.join(', ')})` });
            break;
          }
          case 3: alpha *= R.next(); break; // ALPHA
          case 4: { // CLIP_RECT
            const [x, y, w, h] = R.rect();
            wrap({ clipPath: `path('M${x} ${y}H${x + w}V${y + h}H${x}Z')` });
            break;
          }
          case 5: { // CLIP_PATH
            const rule = R.next();
            wrap({ clipPath: `path(${rule ? 'evenodd, ' : ''}'${R.path()}')` });
            break;
          }
          case 6: { // FILL_PATH
            const rule = R.next(), d = R.path(), s = target();
            GRWeb.svg(s, 'path', { d, fill: GRWeb.fill(s, R.paint()), 'fill-rule': rule ? 'evenodd' : 'nonzero', opacity: opacity() });
            break;
          }
          case 7: { // STROKE_PATH
            const d = R.path(), stroke = R.stroke(), s = target();
            if (stroke) GRWeb.svg(s, 'path', { d, ...GRWeb.strokeAttributes(s, stroke), opacity: opacity() });
            break;
          }
          case 8: { // IMAGE
            const image = R.image();
            const c = GRWeb.imageElement(GRWeb.acquireImage(image), R.rect());
            GRWeb.releaseImage(image.id);
            if (alpha < 1) c.style.opacity = alpha;
            group.append(c);
            svg = null;
            break;
          }
          case 9: // TEXT_LAYOUT
          case 10: { // TEXT_GLYPHS
            const id = R.next(), x = R.next(), y = R.next(), color = R.color();
            const [line, first, end] = op === 10 ? [R.next(), R.next(), R.next()] : [-1, 0, 0];
            const e = GRText.render(id, x, y, color, line, first, end);
            if (alpha < 1) e.style.opacity = alpha;
            group.append(e);
            svg = null;
            break;
          }
          default:
            throw new Error('graphite: bad draw op ' + op);
        }
      }
      return root;
    },

    content(R, node) {
      const kind = R.next();
      let image = 0, element = null;
      switch (kind) {
        case 0: break; // NONE
        case 1: { // IMAGE
          const source = R.image(), rect = R.rect();
          image = source.id;
          const canvas = GRWeb.acquireImage(source); // Acquire before releasing the old one: they may be the same.
          element = GRWeb.imageElement(canvas, rect);
          break;
        }
        case 2: element = GRWeb.drawing(R, node); break; // DRAWN
        case 3: { // SHAPE: local space, not clipped to the bounds.
          const d = R.next() ? R.path() : null;
          const fill = R.paint(), rule = R.next(), stroke = R.stroke(), start = R.next(), end = R.next();
          element = GRWeb.canvasSvg();
          element.style.transform = `translate(${node.offset[0]}px, ${node.offset[1]}px)`;
          element.grLocal = element;
          if (d) {
            GRWeb.svg(element, 'path', { d, fill: GRWeb.fill(element, fill), 'fill-rule': rule ? 'evenodd' : 'nonzero' });
            if (stroke && end > start) {
              const attrs = GRWeb.strokeAttributes(element, stroke);
              // TODO: trimming a dashed stroke.
              if ((start > 0 || end < 1) && !stroke.dash.length)
                Object.assign(attrs, { pathLength: 1, 'stroke-dasharray': `${end - start} 2`, 'stroke-dashoffset': -start });
              GRWeb.svg(element, 'path', { d, ...attrs });
            }
          }
          break;
        }
        case 4: { // GRADIENT: unit space over the box.
          const paint = R.paint();
          element = GRWeb.canvasSvg();
          element.grRect = GRWeb.svg(element, 'rect', {
            width: node.size[0], height: node.size[1], fill: GRWeb.fill(element, paint, 'objectBoundingBox'),
          });
          break;
        }
        default:
          throw new Error('graphite: bad content ' + kind);
      }
      if (node.image) GRWeb.releaseImage(node.image);
      node.image = image;
      GRWeb.setContent(node, element);
    },

    shadow(R, node) {
      node.shadow?.remove();
      node.shadow = null;
      if (!R.next()) return;
      const color = R.color(), dx = R.next(), dy = R.next(), sigma = R.next(), d = R.path();
      const s = GRWeb.canvasSvg();
      Object.assign(s.style, { transform: `translate(${dx}px, ${dy}px)`, filter: sigma > 0 ? `blur(${sigma}px)` : '' });
      GRWeb.svg(s, 'path', { d, fill: color });
      node.shadow = s;
      node.el.prepend(s);
    },

    /* A path mask is a clip. A gradient mask is an SVG image of the gradient over the box, transparent outside it; the
     * group takes the box's size for it, because Chromium ignores masks on empty elements and WebKit clips to the
     * border box. */
    mask(R, node) {
      const kind = R.next(), style = node.group.style;
      Object.assign(style, { clipPath: '', maskImage: '', maskSize: '', maskRepeat: '', width: '', height: '' });
      if (kind === 1) {
        const evenOdd = R.next();
        style.clipPath = `path(${evenOdd ? 'evenodd, ' : ''}'${R.path()}')`;
      } else if (kind === 2) {
        const paint = R.paint(), [w, h] = node.size;
        const svg = document.createElementNS(GRWeb.SVG, 'svg');
        svg.setAttribute('width', w);
        svg.setAttribute('height', h);
        GRWeb.svg(svg, 'rect', { width: w, height: h, fill: GRWeb.fill(svg, paint, 'objectBoundingBox') });
        const data = encodeURIComponent(new XMLSerializer().serializeToString(svg));
        Object.assign(style, {
          maskImage: `url("data:image/svg+xml,${data}")`, maskSize: '100% 100%', maskRepeat: 'no-repeat',
          width: `${w}px`, height: `${h}px`,
        });
      }
    },

    appearance(R, node) {
      const opacity = R.next(), hidden = R.next(), masks = R.next(), radius = R.next(), corners = R.next();
      const background = R.next() ? R.color() : (R.color(), '');
      const hasBorder = R.next(), width = R.next(), borderColor = R.color(), blur = R.next();
      const el = node.el;
      el.style.opacity = opacity < 1 ? opacity : '';
      el.style.display = hidden ? 'none' : '';
      el.style.filter = blur > 0 ? `blur(${blur}px)` : '';
      Object.assign(node, {
        radius, corners, masks: !!masks, fill: background, stroke: hasBorder && width > 0 ? [width, borderColor] : null,
      });
      GRWeb.decorate(node);
    },

    /* Calls into C from a browser callback. Emscripten's exit and unwind exceptions end there; unlike
     * callUserCallback, nothing exits the runtime implicitly afterwards, because events also fire synchronously while
     * C is calling in (focus() from request_focus, the first viewport report from attach). */
    call(f) {
#if EXIT_RUNTIME
      if (runtimeExited) return false;
#endif
      if (ABORT) return false;
      try {
        return f();
      } catch (e) {
        handleException(e);
        return false;
      }
    },

    /* The animation-frame tick: one per host, delivering a requested frame and then the commit (gr_web_tick). */
    requestTick(host) {
      if (host.frame) return;
      host.frame = requestAnimationFrame((ms) => {
        host.frame = 0;
        const time = ms / 1000;
        // Estimate the refresh interval from consecutive frames; 1/60 s until there are two.
        const delta = time - host.last;
        if (host.last && delta > 0.003 && delta < 0.1) host.interval += (delta - host.interval) * 0.25;
        host.last = time;
        // Scale changes are not always announced (emulation fires neither media queries nor resize), so catch them
        // here too, before the frame.
        if (host.events && host.scale !== devicePixelRatio) host.viewport?.();
        GRWeb.call(() => _gr_web_tick(host.backend, host.events, time, host.interval));
      });
    },

    /* W3C UI Events code values in gr_key_code order, and aliases some browsers still report. */
    codes: [''].concat(
      [...'ABCDEFGHIJKLMNOPQRSTUVWXYZ'].map((c) => 'Key' + c), [...'0123456789'].map((d) => 'Digit' + d),
      ['Minus', 'Equal', 'BracketLeft', 'BracketRight', 'Backslash', 'Semicolon', 'Quote', 'Backquote', 'Comma',
        'Period', 'Slash', 'IntlBackslash', 'IntlRo', 'IntlYen', 'Enter', 'Escape', 'Backspace', 'Tab', 'Space',
        'CapsLock', 'ShiftLeft', 'ShiftRight', 'ControlLeft', 'ControlRight', 'AltLeft', 'AltRight', 'MetaLeft',
        'MetaRight', 'ContextMenu', 'Fn', 'ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Insert', 'Delete', 'Home',
        'End', 'PageUp', 'PageDown', 'PrintScreen', 'ScrollLock', 'Pause', 'NumLock'],
      [...'0123456789'].map((d) => 'Numpad' + d),
      ['NumpadAdd', 'NumpadSubtract', 'NumpadMultiply', 'NumpadDivide', 'NumpadDecimal', 'NumpadComma', 'NumpadEqual',
        'NumpadEnter'],
      Array.from({ length: 24 }, (_, i) => 'F' + (i + 1)),
      ['Lang1', 'Lang2', 'KanaMode', 'Convert', 'NonConvert', 'AudioVolumeMute', 'AudioVolumeDown', 'AudioVolumeUp']),
    codeAliases: { OSLeft: 'MetaLeft', OSRight: 'MetaRight', VolumeMute: 'AudioVolumeMute',
      VolumeDown: 'AudioVolumeDown', VolumeUp: 'AudioVolumeUp' },
    /* Key values of the named keys, from GR_KEY_ENTER (3) on in gr_key order. */
    keys: ['Enter', 'Tab', 'Backspace', 'Escape', 'Delete', 'Insert', 'Home', 'End', 'PageUp', 'PageDown', 'ArrowLeft',
      'ArrowRight', 'ArrowUp', 'ArrowDown', 'Shift', 'Control', 'Alt', 'Meta', 'CapsLock', 'NumLock', 'ScrollLock',
      'Fn', 'ContextMenu', 'PrintScreen', 'Pause'].concat(Array.from({ length: 24 }, (_, i) => 'F' + (i + 1))),
    keyAliases: { OS: 'Meta', Super: 'Meta', Hyper: 'Meta', AltGraph: 'Alt' },
    cursors: ['default', 'pointer', 'text', 'crosshair', 'grab', 'grabbing', 'ew-resize', 'ns-resize', 'nesw-resize',
      'nwse-resize', 'not-allowed', 'none'],
    /* The keyboard layout's unshifted characters by code, where the browser exposes them (Chromium). */
    layout: null,

    modifiers(e) {
      return (e.shiftKey ? 1 : 0) | (e.ctrlKey ? 2 : 0) | (e.altKey ? 4 : 0) | (e.metaKey ? 8 : 0) |
        (e.getModifierState?.('CapsLock') ? 16 : 0);
    },

    /* Starts delivering a host's DOM events to its event host. Each call into C goes through GRWeb.call and checks
     * host.events first: a handler may release the event host (which clears it) or tear the Scene down. */
    listen(host) {
      const element = host.element;
      element.tabIndex = 0;
      Object.assign(element.style, { outline: 'none', touchAction: 'none', userSelect: 'none', webkitUserSelect: 'none' });
      if (!GRWeb.layout && navigator.keyboard?.getLayoutMap)
        navigator.keyboard.getLayoutMap().then((map) => { GRWeb.layout = map; }, () => {});
      const call = (f) => (host.events ? GRWeb.call(f) : false);
      const point = (e) => {
        const r = element.getBoundingClientRect();
        return [e.clientX - r.left, e.clientY - r.top];
      };
      const viewport = () => {
        const r = element.getBoundingClientRect();
        host.scale = devicePixelRatio;
        call(() => _gr_web_on_viewport(host.events, r.width, r.height, devicePixelRatio || 1));
      };
      host.viewport = viewport;
      // devicePixelRatio changes with page zoom and when the window moves between displays.
      const watchScale = () => {
        const query = matchMedia(`(resolution: ${devicePixelRatio || 1}dppx)`);
        const changed = () => {
          query.removeEventListener('change', changed);
          viewport();
          if (host.listeners) watchScale();
        };
        query.addEventListener('change', changed);
        host.unwatchScale = () => {
          query.removeEventListener('change', changed);
          removeEventListener('resize', resized);
        };
      };
      const resized = () => {
        if (host.scale !== devicePixelRatio) viewport();
      };
      addEventListener('resize', resized);
      const resize = new ResizeObserver(() => viewport());
      resize.observe(element);
      watchScale();

      // Pointers: mouse and pen. DOM buttons 0-4 are primary, middle, secondary, back, forward; buttons bits are
      // primary, secondary, middle, back, forward.
      const BUTTON = [1, 3, 2, 4, 5], BITS = [1, 2, 3, 4, 5];
      const state = { inside: false, buttons: 0, clicks: 0, button: 0, time: 0, x: 0, y: 0, menu: false };
      const buttonsOf = (b) => BITS.reduce((bits, button, i) => (b & (1 << i) ? bits | (1 << button) : bits), 0);
      const kindOf = (e) => (e.pointerType === 'mouse' ? 0 : e.pointerType === 'pen' ? 1 : -1);
      const send = (type, e, button = 0, clicks = 0) => {
        const [x, y] = point(e), kind = kindOf(e);
        const pressure = kind === 1 ? e.pressure : state.buttons ? 0.5 : 0;
        return call(() => _gr_web_on_pointer(host.events, type, e.timeStamp / 1000, GRWeb.modifiers(e),
          kind === 1 ? e.pointerId : 0, kind, x, y, button, state.buttons, clicks, pressure));
      };
      const hover = (inside, e) => {
        if (inside === state.inside) return;
        state.inside = inside;
        send(inside ? 7 : 8, e);
      };
      const contains = (e) => {
        const r = element.getBoundingClientRect();
        return e.clientX >= r.left && e.clientY >= r.top && e.clientX < r.right && e.clientY < r.bottom;
      };
      // Counts consecutive presses of one button within 500 ms and 5 points, as a double-click setting would.
      const count = (button, e) => {
        const [x, y] = point(e), time = e.timeStamp;
        const again = state.clicks && button === state.button && time - state.time <= 500 &&
          Math.abs(x - state.x) <= 5 && Math.abs(y - state.y) <= 5;
        Object.assign(state, { clicks: again ? state.clicks + 1 : 1, button, time, x, y });
        return state.clicks;
      };
      // A press, a release, or a chorded button change reported with a move.
      const press = (e) => {
        const button = BUTTON[e.button] ?? 0, before = state.buttons;
        state.buttons = buttonsOf(e.buttons);
        const down = !(before & (1 << button)) && !!(state.buttons & (1 << button));
        if (down) {
          element.focus({ preventScroll: true });
          // Capture keeps moves and the release coming during drags, as native hosts' implicit grabs do. It throws
          // for pointers the browser no longer tracks.
          if (e.type === 'pointerdown') {
            try {
              element.setPointerCapture(e.pointerId);
            } catch {}
          }
        }
        const handled = send(down ? 4 : 5, e, button, down ? count(button, e) : state.clicks);
        if (down && button === 2) state.menu = handled;
        // Crossings are not reported during drags; catch up once the last button is released.
        if (!state.buttons) hover(contains(e), e);
        return handled;
      };
      const on = (type, handler, options) => {
        element.addEventListener(type, handler, options);
        host.listeners.push([type, handler, options]);
      };
      host.listeners = [];
      host.unobserve = () => resize.disconnect();
      on('pointerdown', (e) => {
        if (kindOf(e) >= 0 && BUTTON[e.button] && press(e)) e.preventDefault();
      });
      on('pointerup', (e) => {
        if (kindOf(e) >= 0 && BUTTON[e.button] && press(e)) e.preventDefault();
      });
      on('pointermove', (e) => {
        if (kindOf(e) < 0) return;
        if (e.button >= 0 && BUTTON[e.button]) {
          if (press(e)) e.preventDefault();
          return;
        }
        if (!state.buttons) hover(true, e);
        if (send(6, e)) e.preventDefault();
      });
      on('pointerenter', (e) => {
        if (kindOf(e) >= 0 && !state.buttons) hover(true, e);
      });
      on('pointerleave', (e) => {
        if (kindOf(e) >= 0 && !state.buttons) hover(false, e);
      });
      on('pointercancel', (e) => {
        if (kindOf(e) < 0 || !state.buttons) return;
        state.buttons = 0;
        send(9, e);
      });
      on('contextmenu', (e) => {
        if (state.menu) e.preventDefault();
      });
      on('wheel', (e) => {
        // Pixel deltas are CSS pixels, which are logical points; line and page deltas both become lines.
        const [x, y] = point(e), unit = e.deltaMode === 0 ? 0 : 1;
        if (call(() => _gr_web_on_scroll(host.events, e.timeStamp / 1000, GRWeb.modifiers(e), x, y, e.deltaX, e.deltaY,
          unit))) e.preventDefault();
      }, { passive: false });
      const key = (e, down) => {
        const name = GRWeb.codeAliases[e.code] ?? e.code;
        const code = Math.max(0, GRWeb.codes.indexOf(name));
        const value = GRWeb.keyAliases[e.key] ?? e.key;
        const composing = e.isComposing || e.key === 'Process' || e.keyCode === 229;
        const index = GRWeb.keys.indexOf(value);
        let named = value === 'Process' ? 2 : index >= 0 ? index + 3 : 0, character = 0;
        if (!named) {
          // The character the key types without modifiers: from the layout map, else the key value lowercased.
          const plain = GRWeb.layout?.get(e.code) ?? ([...e.key].length === 1 ? e.key.toLowerCase() : '');
          const cp = [...plain].length === 1 ? plain.codePointAt(0) : 0;
          if (cp >= 0x20 && cp !== 0x7f) {
            character = cp;
            named = 1;
          }
        }
        if (call(() => _gr_web_on_key(host.events, down, e.timeStamp / 1000, GRWeb.modifiers(e), code, named,
          character, e.repeat, composing, e.keyCode >>> 0))) e.preventDefault();
      };
      on('keydown', (e) => key(e, true));
      on('keyup', (e) => key(e, false));
      on('focus', (e) => call(() => _gr_web_on_focus(host.events, true, e.timeStamp / 1000)));
      on('blur', (e) => call(() => _gr_web_on_focus(host.events, false, e.timeStamp / 1000)));
      viewport();
      if (document.activeElement === element && document.hasFocus())
        call(() => _gr_web_on_focus(host.events, true, performance.now() / 1000));
    },

    unlisten(host) {
      for (const [type, handler, options] of host.listeners ?? []) host.element.removeEventListener(type, handler, options);
      host.listeners = null;
      host.viewport = null;
      host.unobserve?.();
      host.unwatchScale?.();
    },

    geometry(R, node) {
      const w = R.next(), h = R.next();
      const m = [R.next(), R.next(), R.next(), R.next(), R.next(), R.next()];
      node.offset = [R.next(), R.next()];
      node.el.style.transform = `matrix(${m.join(', ')})`;
      const translate = `translate(${node.offset[0]}px, ${node.offset[1]}px)`;
      node.sublayers.style.transform = translate;
      const content = node.content;
      if (content?.grLocal) content.grLocal.style.transform = translate;
      if (content?.grRect) {
        content.grRect.setAttribute('width', w);
        content.grRect.setAttribute('height', h);
      }
      if (content?.grClip) content.style.clipPath = `path('M0 0H${w}V${h}H0Z')`;
      if (w !== node.size[0] || h !== node.size[1]) {
        node.size = [w, h];
        GRWeb.decorate(node);
      }
    },
  },

  // Targets resolve like Emscripten's html5 API: a name registered in specialHTMLTargets (for example
  // specialHTMLTargets['!editor'] = element), else a CSS selector. Only elements qualify, not document or window.
  gr_web_js_host_new__deps: ['$GRWeb', '$UTF8ToString', '$specialHTMLTargets'],
  gr_web_js_host_new: (target) => {
    const name = UTF8ToString(target);
    let parent = specialHTMLTargets[name];
    if (!parent) {
      try {
        parent = document.querySelector(name);
      } catch {
        return 0;
      }
    }
    if (!(parent instanceof Element)) return 0;
    const element = document.createElement('div');
    element.className = 'gr-host';
    Object.assign(element.style, { position: 'relative', width: '100%', height: '100%', overflow: 'hidden' });
    parent.append(element);
    const handle = GRWeb.nextHost++;
    GRWeb.hosts.set(handle, {
      element, nodes: new Map(), frame: 0, backend: 0, events: 0, listeners: null, last: 0, interval: 1 / 60,
    });
    return handle;
  },

  gr_web_js_host_backend__deps: ['$GRWeb'],
  gr_web_js_host_backend: (handle, backend) => {
    GRWeb.hosts.get(handle).backend = backend;
  },

  gr_web_js_host_events__deps: ['$GRWeb', '$handleException', 'gr_web_on_viewport', 'gr_web_on_focus',
    'gr_web_on_pointer', 'gr_web_on_scroll', 'gr_web_on_key'],
  gr_web_js_host_events: (handle, events) => {
    const host = GRWeb.hosts.get(handle);
    if (!host) return;
    host.events = events;
    if (events && !host.listeners) GRWeb.listen(host);
    else if (!events) GRWeb.unlisten(host);
  },

  gr_web_js_now: () => performance.now() / 1000,

  gr_web_js_set_cursor__deps: ['$GRWeb'],
  gr_web_js_set_cursor: (handle, cursor) => {
    const host = GRWeb.hosts.get(handle);
    if (host) host.element.style.cursor = GRWeb.cursors[cursor] ?? '';
  },

  gr_web_js_request_focus__deps: ['$GRWeb'],
  gr_web_js_request_focus: (handle) => GRWeb.hosts.get(handle)?.element.focus({ preventScroll: true }),

  gr_web_js_host_free__deps: ['$GRWeb'],
  gr_web_js_host_free: (handle) => {
    const host = GRWeb.hosts.get(handle);
    if (!host) return;
    if (host.frame) cancelAnimationFrame(host.frame);
    GRWeb.unlisten(host);
    for (const node of host.nodes.values()) if (node.image) GRWeb.releaseImage(node.image);
    host.element.remove();
    GRWeb.hosts.delete(handle);
  },

  gr_web_js_device_pixel_ratio: () => globalThis.devicePixelRatio || 1,

  gr_web_js_request_tick__deps: ['$GRWeb', '$handleException', 'gr_web_tick'],
  gr_web_js_request_tick: (handle) => {
    const host = GRWeb.hosts.get(handle);
    if (host) GRWeb.requestTick(host);
  },

  gr_web_js_apply__deps: ['$GRWeb'],
  gr_web_js_apply: (handle, commands, count) => {
    const host = GRWeb.hosts.get(handle);
    const R = GRWeb.reader(HEAPF64.subarray(commands >> 3, (commands >> 3) + count));
    const nodes = host.nodes;
    const children = [];
    // Children apply in two passes once every CHILDREN command is read: detach the children missing from their
    // parent's new list (only where they are still in that parent), then install every new list.
    const installChildren = () => {
      for (const { parent, ids } of children) {
        const keep = new Set(ids);
        for (const el of [...parent.sublayers.children]) if (!keep.has(el.grId)) el.remove();
      }
      for (const { parent, ids } of children) for (const id of ids) parent.sublayers.append(nodes.get(id).el);
      children.length = 0;
    };
    while (!R.done) {
      const op = R.next();
      if (op !== 3) installChildren();
      switch (op) {
        case 1: { // ROOT
          const id = R.next();
          const root = GRWeb.node(id);
          nodes.set(id, root);
          host.element.append(root.el);
          break;
        }
        case 2: { // REALIZE
          const id = R.next();
          R.next(); // kind
          if (!nodes.has(id)) nodes.set(id, GRWeb.node(id));
          break;
        }
        case 3: { // CHILDREN
          const parent = nodes.get(R.next());
          const ids = [];
          for (let n = R.next(); n > 0; n--) ids.push(R.next());
          children.push({ parent, ids });
          break;
        }
        case 4: { // RELEASE
          const id = R.next();
          const node = nodes.get(id);
          if (node) {
            node.el.remove();
            if (node.image) GRWeb.releaseImage(node.image);
          }
          nodes.delete(id);
          break;
        }
        case 5: GRWeb.geometry(R, nodes.get(R.next())); break;
        case 6: GRWeb.appearance(R, nodes.get(R.next())); break;
        case 7: GRWeb.shadow(R, nodes.get(R.next())); break;
        case 8: GRWeb.content(R, nodes.get(R.next())); break;
        case 9: GRWeb.mask(R, nodes.get(R.next())); break;
        default:
          throw new Error('graphite: bad command ' + op);
      }
    }
    installChildren();
  },
});
