// Graphite Web text system: the DOM side of text.c, linked with --js-library.
//
// A layout is computed on first use in a hidden measurement element:
// 1. Line breaks: the text wraps in one block of the layout's width (when it can wrap), and a grapheme cluster
//    starts a new line when its box moves below the current line. Paragraph separators always end lines.
// 2. Truncation: max_lines and end ellipses are applied by hand, because browsers report neither what an
//    ellipsis cut nor where it went.
// 3. Line boxes: each line is laid out again as its own block (white-space: pre, explicit direction), which gives
//    its box, its baseline (from a zero-size inline-block marker) and its width.
// Drawing places the same line blocks at the measured positions, so what is measured is what is drawn. Cluster
// boxes for carets, hit testing, selections and glyphs are measured per line when a query first needs them.
//
// Indices are UTF-16 internally and UTF-8 byte offsets at the C boundary.

addToLibrary({
  $GRText: {
    layouts: new Map(),
    next: 1,
    host: null,
    canvas: null,
    graphemes: null,
    decoder: null,

    /* Character classes. Emscripten copies library objects as source but turns regular expression properties into
     * empty objects, so these are functions. RTL covers right-to-left scripts (Hebrew, Arabic, Syriac, Thaana, NKo,
     * Samaritan, Mandaic and their presentation forms); LETTER the strong letters and digits of everything else. */
    RTL: (s) => /[\u0590-\u08FF\uFB1D-\uFDFF\uFE70-\uFEFF\u{10800}-\u{10FFF}\u{1E800}-\u{1EFFF}]/u.test(s),
    LETTER: (s) => /[\p{L}\p{Nd}]/u.test(s),
    EMOJI: (s) => /\p{Extended_Pictographic}|\p{Regional_Indicator}/u.test(s),
    SEPARATOR: (s) => /^(\r\n|\n|\r|\u2029)$/.test(s),
    WHITESPACE: (s) => /^[ \t]+$/.test(s),
    ELLIPSIS: '\u2026',

    /* The hidden element that layouts are measured in. */
    measure() {
      if (!GRText.host?.isConnected) {
        const d = document.createElement('div');
        Object.assign(d.style, {
          position: 'absolute', left: '0', top: '0', width: '0', height: '0', overflow: 'hidden',
          visibility: 'hidden', pointerEvents: 'none', contain: 'layout paint style',
        });
        document.body.append(d);
        GRText.host = d;
      }
      return GRText.host;
    },

    decode(pointer, length) {
      if (!length) return '';
      GRText.decoder ??= new TextDecoder();
      return GRText.decoder.decode(HEAPU8.slice(pointer, pointer + length));
    },

    family(name) {
      const generic = { 'system-ui': 'system-ui', 'sans-serif': 'sans-serif', serif: 'serif',
        monospace: 'ui-monospace, monospace' };
      return generic[name] ?? `"${name.replace(/["\\]/g, '\\$&')}", system-ui`;
    },

    font(s) {
      return `${s.slant ? 'italic ' : ''}${s.weight} ${s.size}px ${GRText.family(s.family)}`;
    },

    /* 32-bit FNV-1a, never 0. */
    hash(text) {
      let h = 0x811c9dc5;
      for (let i = 0; i < text.length; i++) h = Math.imul(h ^ text.charCodeAt(i), 0x01000193);
      return (h >>> 0) || 1;
    },

    /* Parses the operands text.c encodes into a layout description; nothing is measured yet. */
    create(utf8, length, operands, count) {
      const w = HEAPF64.subarray(operands >> 3, (operands >> 3) + count);
      let i = 0;
      const style = () => ({
        size: w[i++], weight: w[i++], slant: w[i++], decorations: w[i++], letterSpacing: w[i++],
        family: GRText.decode(w[i++], w[i++]),
      });
      const options = {
        hasWidth: !!w[i++], width: w[i++], wrap: w[i++], maxLines: w[i++], truncate: w[i++] === 1, align: w[i++],
        direction: w[i++],
      };
      const base = { ...style(), color: null };
      const spans = [];
      for (let n = w[i++]; n > 0; n--) {
        spans.push({ start: w[i++], end: w[i++], fields: w[i++], style: style(), color: [w[i++], w[i++], w[i++], w[i++]] });
      }
      const text = GRText.decode(utf8, length);
      // UTF-16 index to UTF-8 offset, and back for code point boundaries.
      const u8 = new Float64Array(text.length + 1), u16 = new Map();
      let bytes = 0;
      for (let k = 0; k < text.length; k++) {
        u8[k] = bytes;
        u16.set(bytes, k);
        const c = text.codePointAt(k);
        bytes += c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
        if (c >= 0x10000) u8[++k] = u8[k - 1];
      }
      u8[text.length] = bytes;
      u16.set(bytes, text.length);
      const L = { text, base, spans, options, u8, u16, computed: false };
      L.toU16 = (offset) => {
        while (offset > 0 && !u16.has(offset)) offset--;
        return u16.get(offset) ?? 0;
      };
      return L;
    },

    /* Style runs, grapheme clusters and paragraphs. */
    prepare(L) {
      const { text, base, spans } = L;
      const cuts = new Set([0, text.length]);
      for (const s of spans) {
        s.start16 = L.toU16(s.start);
        s.end16 = L.toU16(s.end);
        cuts.add(s.start16);
        cuts.add(s.end16);
      }
      const points = [...cuts].sort((a, b) => a - b);
      L.runs = [];
      for (let k = 0; k + 1 < points.length; k++) {
        const a = points[k], b = points[k + 1];
        const s = { ...base };
        // Base style, then each span's selected fields in order: later spans win.
        for (const span of spans) {
          if (span.start16 > a || span.end16 < b) continue;
          const f = span.fields;
          if (f & 1) s.family = span.style.family;
          if (f & 2) s.size = span.style.size;
          if (f & 4) s.weight = span.style.weight;
          if (f & 8) s.slant = span.style.slant;
          if (f & 16) s.color = span.color;
          if (f & 32) s.decorations = span.style.decorations;
          if (f & 64) s.letterSpacing = span.style.letterSpacing;
        }
        L.runs.push({ start: a, end: b, style: s });
      }
      if (!L.runs.length) L.runs.push({ start: 0, end: 0, style: { ...base } });
      GRText.graphemes ??= new Intl.Segmenter(undefined, { granularity: 'grapheme' });
      L.graphemes = [];
      for (const { segment, index } of GRText.graphemes.segment(text))
        L.graphemes.push({ s: index, e: index + segment.length, text: segment, separator: GRText.SEPARATOR(segment) });
      // Paragraphs: [start, content end, end after the separator] and their direction.
      L.paragraphs = [];
      let start = 0;
      for (const g of L.graphemes) {
        if (!g.separator) continue;
        L.paragraphs.push({ start, contentEnd: g.s, end: g.e });
        start = g.e;
      }
      L.paragraphs.push({ start, contentEnd: text.length, end: text.length });
      for (const p of L.paragraphs) p.rtl = GRText.paragraphRTL(L, p);
      // The last grapheme of each paragraph gets no letter spacing after it.
      L.lastOfParagraph = new Set();
      for (const p of L.paragraphs) {
        const last = L.graphemes.filter((g) => g.s >= p.start && g.e <= p.contentEnd).pop();
        if (last) L.lastOfParagraph.add(last.s);
      }
      L.boundaries8 = [0, ...L.graphemes.map((g) => L.u8[g.e])];
    },

    paragraphRTL(L, p) {
      if (L.options.direction) return L.options.direction === 2;
      for (const ch of L.text.slice(p.start, p.contentEnd)) {
        if (GRText.RTL(ch)) return true;
        if (GRText.LETTER(ch) && !/\p{Nd}/u.test(ch)) return false;
      }
      return false;
    },

    paragraphAt(L, index) {
      return L.paragraphs.find((p) => index >= p.start && index < p.end) ?? L.paragraphs[L.paragraphs.length - 1];
    },

    runAt(L, index) {
      return L.runs.find((r) => index >= r.start && index < r.end) ?? L.runs[L.runs.length - 1];
    },

    /* Spans for text [from, to). paint is null when measuring; when drawing it holds the default color and whether
     * decorations draw. Measuring splits runs only where the layout changes: every element boundary can move later
     * text by a layout unit (1/64 px in Chromium), and color is paint-only. */
    spans(L, from, to, paint) {
      const segments = [];
      const key = (run) => (paint ? run : GRText.font(run.style) + '|' + run.style.letterSpacing);
      for (const g of L.graphemes) {
        if (g.s < from || g.e > to) continue;
        const run = GRText.runAt(L, g.s), last = L.lastOfParagraph.has(g.s);
        const top = segments[segments.length - 1];
        if (top && top.key === key(run) && top.last === last) top.e = g.e;
        else segments.push({ key: key(run), run, last, s: g.s, e: g.e });
      }
      return segments.map((seg) => {
        const span = document.createElement('span');
        span.textContent = L.text.slice(seg.s, seg.e);
        GRText.styleSpan(span, seg.run.style, paint, seg.last);
        return span;
      });
    },

    styleSpan(span, s, paint, last = false) {
      span.style.font = GRText.font(s);
      if (s.letterSpacing && !last) span.style.letterSpacing = s.letterSpacing + 'px';
      if (!paint) return;
      const c = s.color;
      span.style.color = c ? `rgb(${c[0] * 255} ${c[1] * 255} ${c[2] * 255} / ${c[3]})` : paint.color;
      if (paint.decorations && s.decorations & 1) span.style.textDecorationLine = 'underline';
    },

    /* A line as a block: a baseline marker, then its content and any ellipsis. The block's font, which sets the
     * line's minimum height (its strut), is the style where the line starts, as native text systems size lines by
     * the fonts in them. */
    lineElement(L, line, paint) {
      const d = document.createElement('div');
      d.style.font = GRText.font(GRText.runAt(L, Math.min(line.start, Math.max(0, L.text.length - 1))).style);
      Object.assign(d.style, { whiteSpace: 'pre', width: 'max-content', direction: line.rtl ? 'rtl' : 'ltr' });
      const marker = document.createElement('span');
      Object.assign(marker.style, { display: 'inline-block', width: '0', height: '0' });
      d.append(marker, ...GRText.spans(L, line.start, line.contentEnd, paint));
      if (line.ellipsis) {
        const e = document.createElement('span');
        e.textContent = GRText.ELLIPSIS;
        GRText.styleSpan(e, line.ellipsis.style, paint);
        d.append(e);
        line.ellipsisNode = e;
      }
      return { element: d, marker };
    },

    /* The longest grapheme prefix of [start, end) that fits width with an ellipsis after it. */
    ellipsize(L, line, start, end, width) {
      const graphemes = L.graphemes.filter((g) => g.s >= start && g.e <= end);
      const host = GRText.measure();
      const trimmed = (k) => {
        let e = k ? graphemes[k - 1].e : start;
        while (e > start && /\s/.test(L.text[e - 1])) e--;
        return e;
      };
      const style = (e) => GRText.runAt(L, Math.max(start, e - 1)).style;
      const fits = (k) => {
        const e = trimmed(k);
        const { element } = GRText.lineElement(L, { ...line, contentEnd: e, ellipsis: { style: style(e) } }, null);
        host.append(element);
        const w = element.getBoundingClientRect().width;
        element.remove();
        return width === undefined || w <= width + 1e-6;
      };
      let lo = 0, hi = graphemes.length;
      if (width === undefined) lo = hi;
      while (lo < hi) {
        const mid = (lo + hi + 1) >> 1;
        if (fits(mid)) lo = mid;
        else hi = mid - 1;
      }
      const e = trimmed(lo);
      line.contentEnd = e;
      line.ellipsis = { style: style(e), elidedStart: e, elidedEnd: end };
    },

    /* Steps 1 to 3 of the file comment. */
    compute(L) {
      if (L.computed) return L;
      L.computed = true;
      GRText.prepare(L);
      const { text, options } = L;
      const host = GRText.measure();
      let lines = [];
      if (options.hasWidth && options.wrap !== 1) {
        // 1. Wrap in one block and follow the cluster boxes down.
        const block = document.createElement('div');
        block.style.font = GRText.font(L.base);
        Object.assign(block.style, {
          width: options.width + 'px', whiteSpace: 'pre-wrap', direction: options.direction === 2 ? 'rtl' : 'ltr',
          unicodeBidi: options.direction ? 'isolate' : 'plaintext',
          overflowWrap: 'break-word',
        });
        const spans = GRText.spans(L, 0, text.length, null);
        block.append(...spans);
        host.append(block);
        const nodes = [];
        let at = 0;
        for (const span of spans) {
          nodes.push({ node: span.firstChild, start: at });
          at += span.textContent.length;
        }
        const range = document.createRange();
        const box = block.getBoundingClientRect();
        let line = null, force = true;
        for (const g of L.graphemes) {
          let rect = null;
          if (!g.separator) {
            const n = nodes.findLast((x) => x.start <= g.s);
            range.setStart(n.node, g.s - n.start);
            range.setEnd(n.node, Math.min(g.e - n.start, n.node.length));
            const rects = range.getClientRects();
            // WebKit adds a zero-width rect at the previous line's end for a cluster after a break.
            const r = [...rects].find((x) => x.width > 0) ?? rects[rects.length - 1];
            if (r) rect = { top: r.top - box.top, bottom: r.bottom - box.top };
          }
          // Cluster boxes can overlap the next line's (WebKit), so compare against half a cluster's height.
          if (force || (rect && line.bottom > -Infinity && rect.top >= line.bottom - (rect.bottom - rect.top) / 2)) {
            line = { start: g.s, end: g.s, bottom: -Infinity };
            lines.push(line);
            force = false;
          }
          line.end = g.e;
          if (rect) line.bottom = Math.max(line.bottom, rect.bottom);
          if (g.separator) force = true;
        }
        if (force) lines.push({ start: text.length, end: text.length });
        block.remove();
      } else {
        lines = L.paragraphs.map((p) => ({ start: p.start, end: p.end }));
      }
      for (const line of lines) {
        const p = GRText.paragraphAt(L, line.start);
        line.rtl = p.rtl;
        line.paragraph = p;
        line.separator = line.end > p.contentEnd;
        // Trailing whitespace and the separator are not laid out: they would shift aligned lines.
        let e = Math.min(line.end, p.contentEnd);
        while (e > line.start && GRText.WHITESPACE(text[e - 1])) e--;
        line.contentEnd = e;
      }
      // 2. Truncation.
      L.truncated = false;
      if (options.maxLines && lines.length > options.maxLines) {
        lines = lines.slice(0, options.maxLines);
        L.truncated = true;
        if (options.truncate) {
          // The ellipsized last line owns the rest of its paragraph.
          const last = lines[lines.length - 1], p = last.paragraph;
          last.end = p.end;
          last.separator = p.end > p.contentEnd;
          GRText.ellipsize(L, last, last.start, p.contentEnd, options.hasWidth ? options.width : undefined);
        }
      }
      // 3. Line boxes, stacked as the browser would stack lines.
      const stack = document.createElement('div');
      host.append(stack);
      const placed = lines.map((line) => {
        const { element, marker } = GRText.lineElement(L, line, null);
        stack.append(element);
        return { line, element, marker };
      });
      // A line that cannot wrap is cut by the width only with an end ellipsis.
      if (options.truncate && options.hasWidth) {
        for (const p of placed) {
          if (p.line.ellipsis || p.element.getBoundingClientRect().width <= options.width + 1e-6) continue;
          L.truncated = true;
          GRText.ellipsize(L, p.line, p.line.start, p.line.paragraph.contentEnd, options.width);
          const fresh = GRText.lineElement(L, p.line, null);
          p.element.replaceWith(fresh.element);
          Object.assign(p, fresh);
        }
      }
      const top = stack.getBoundingClientRect().top;
      for (const { line, element, marker } of placed) {
        const r = element.getBoundingClientRect();
        line.top = r.top - top;
        line.height = r.height;
        line.width = r.width;
        line.baseline = marker.getBoundingClientRect().bottom - top;
      }
      stack.remove();
      L.lines = lines;
      L.width = options.hasWidth ? options.width : Math.max(0, ...lines.map((l) => l.width));
      L.height = lines.length ? lines[lines.length - 1].top + lines[lines.length - 1].height : 0;
      for (const line of lines) {
        // START is left in left-to-right paragraphs and right in right-to-left ones; END is the opposite.
        const free = L.width - line.width;
        line.x = options.align === 1 ? free / 2 : (options.align === 2) !== line.rtl ? free : 0;
      }
      L.visibleEnd = lines[lines.length - 1].end;
      return L;
    },

    /* Per-line cluster boxes in layout coordinates: {s, e, left, right, rtl}, plus the ellipsis box. */
    clusters(L, line) {
      if (line.clusters) return line.clusters;
      const { element } = GRText.lineElement(L, line, null);
      const host = GRText.measure();
      host.append(element);
      const box = element.getBoundingClientRect();
      const nodes = [];
      let at = line.start;
      for (const span of element.childNodes) {
        if (!span.firstChild || span === line.ellipsisNode) continue;
        nodes.push({ node: span.firstChild, start: at });
        at += span.textContent.length;
      }
      const range = document.createRange();
      const rectOf = (node, from, to) => {
        range.setStart(node, from);
        range.setEnd(node, to);
        const rects = range.getClientRects();
        const r = [...rects].find((x) => x.width > 0) ?? rects[0];
        return r ? [r.left - box.left + line.x, r.right - box.left + line.x] : [line.x, line.x];
      };
      const end = line.rtl ? line.x : line.x + line.width;
      const out = [];
      const kinds = [];
      for (const g of L.graphemes) {
        if (g.s < line.start || g.e > line.end || g.separator) continue;
        if (line.ellipsis && g.s >= line.ellipsis.elidedStart) continue;
        let left, right;
        if (g.s >= line.contentEnd) left = right = end; // Trailing whitespace hangs at the line's end.
        else {
          const n = nodes.findLast((x) => x.start <= g.s);
          [left, right] = rectOf(n.node, g.s - n.start, Math.min(g.e - n.start, n.node.length));
        }
        out.push({ s: g.s, e: g.e, left, right, text: g.text });
        kinds.push(GRText.RTL(g.text) ? 'r' : GRText.LETTER(g.text) ? 'l' : 'n');
      }
      // Neutrals take the direction of the strong characters around them when both agree, else the paragraph's.
      for (let k = 0; k < out.length; k++) {
        let kind = kinds[k];
        if (kind === 'n') {
          let before = line.rtl ? 'r' : 'l', after = before;
          for (let j = k - 1; j >= 0; j--) if (kinds[j] !== 'n') { before = kinds[j]; break; }
          for (let j = k + 1; j < out.length; j++) if (kinds[j] !== 'n') { after = kinds[j]; break; }
          kind = before === after ? before : line.rtl ? 'r' : 'l';
        }
        out[k].rtl = kind === 'r';
      }
      if (line.ellipsis) {
        const node = line.ellipsisNode.firstChild;
        const [left, right] = rectOf(node, 0, node.length);
        line.ellipsisBox = { s: line.ellipsis.elidedStart, e: line.ellipsis.elidedEnd, left, right, rtl: line.rtl };
      }
      element.remove();
      line.clusters = out;
      return out;
    },

    /* The line holding index; at a soft line break, upstream picks the earlier line. */
    lineFor(L, index, upstream) {
      const lines = L.lines;
      for (let k = 0; k < lines.length; k++) {
        const line = lines[k], next = lines[k + 1];
        if (index < line.start) continue;
        if (index < line.end || !next) return line;
        if (index === line.end && index === next.start) return upstream && !line.separator ? line : next;
      }
      return lines[lines.length - 1];
    },

    caretX(L, line, index, upstream) {
      const clusters = GRText.clusters(L, line);
      if (line.ellipsis && index >= line.ellipsis.elidedStart) {
        const e = line.ellipsisBox;
        return line.rtl ? e.right : e.left;
      }
      const at = clusters.find((c) => c.s === index), before = clusters.find((c) => c.e === index);
      const leading = (c) => (c.rtl ? c.right : c.left), trailing = (c) => (c.rtl ? c.left : c.right);
      if (upstream && before) return trailing(before);
      if (at) return leading(at);
      if (before) return trailing(before);
      if (line.ellipsis) return line.rtl ? line.ellipsisBox.right : line.ellipsisBox.left;
      return line.rtl ? line.x + line.width : line.x;
    },

    hitTest(L, x, y) {
      const lines = L.lines;
      let line = lines[lines.length - 1];
      for (const l of lines) if (y < l.top + l.height) { line = l; break; }
      const clusters = GRText.clusters(L, line).filter((c) => c.s < line.contentEnd);
      const last = line === lines[lines.length - 1];
      // The end of a soft-wrapped line is upstream of the next line's start; hard breaks stop before the separator.
      const endOf = () => (line.separator || last ? [line.contentEnd, 0] : [line.end, 1]);
      const resolve = (c, after) => {
        const index = after ? c.e : c.s;
        return index >= line.contentEnd ? endOf() : [index, 0];
      };
      const items = [...clusters];
      if (line.ellipsisBox) items.push(line.ellipsisBox);
      if (!items.length) return [line.start, 0];
      items.sort((a, b) => a.left - b.left);
      const first = items[0], end = items[items.length - 1];
      const pick = (c, rightSide) => {
        if (c === line.ellipsisBox) return [c.s, 0];
        return resolve(c, rightSide !== c.rtl);
      };
      if (x < first.left) return pick(first, false);
      if (x >= end.right) return pick(end, true);
      for (const c of items) if (x >= c.left && x < c.right) return pick(c, x >= (c.left + c.right) / 2);
      // In a gap: the nearer neighbor.
      let best = first, distance = Infinity;
      for (const c of items) {
        const d = Math.min(Math.abs(x - c.left), Math.abs(x - c.right));
        if (d < distance) { distance = d; best = c; }
      }
      return pick(best, x >= (best.left + best.right) / 2);
    },

    rangeRects(L, start, end) {
      const out = [];
      for (const line of L.lines) {
        if (end <= line.start || start >= line.end) continue;
        const pieces = GRText.clusters(L, line).filter((c) => c.s >= start && c.e <= end).map((c) => [c.left, c.right]);
        const e = line.ellipsisBox;
        if (e && start < e.e && end > e.s) pieces.push([e.left, e.right]);
        pieces.sort((a, b) => a[0] - b[0]);
        const merged = [];
        for (const p of pieces) {
          const top = merged[merged.length - 1];
          if (top && p[0] <= top[1] + 0.5) top[1] = Math.max(top[1], p[1]);
          else merged.push([...p]);
        }
        for (const [left, right] of merged) out.push([left, line.top, right - left, line.height]);
      }
      return out;
    },

    /* Glyphs of a line in visual order: one per grapheme cluster, and the ellipsis. */
    glyphs(L, line) {
      if (line.glyphs) return line.glyphs;
      const clusters = GRText.clusters(L, line);
      GRText.canvas ??= document.createElement('canvas').getContext('2d');
      const ctx = GRText.canvas;
      const glyph = (c, style, flags, cluster8) => {
        const font = GRText.font(style);
        let ink = [c.left, line.baseline, 0, 0];
        if (!/^\s*$/u.test(c.text)) {
          ctx.font = font;
          const m = ctx.measureText(c.text);
          ink = [c.left - m.actualBoundingBoxLeft, line.baseline - m.actualBoundingBoxAscent,
            m.actualBoundingBoxLeft + m.actualBoundingBoxRight, m.actualBoundingBoxAscent + m.actualBoundingBoxDescent];
        }
        // Fonts: the face's style, and emoji from their fallback font. Glyphs: the cluster's text.
        const face = GRText.hash(font + (GRText.EMOJI(c.text) ? '|emoji' : ''));
        return { c, values: [c.left, line.baseline, c.right - c.left, ...ink, ...cluster8, flags, face, style.size,
          GRText.hash(c.text)] };
      };
      const list = clusters.map((c) => glyph(c, GRText.runAt(L, c.s).style, c.rtl ? 1 : 0, [L.u8[c.s], L.u8[c.e]]));
      if (line.ellipsisBox) {
        const e = line.ellipsisBox;
        list.push(glyph({ ...e, text: GRText.ELLIPSIS }, line.ellipsis.style, 2 | (line.rtl ? 1 : 0),
          [L.u8[e.s], L.u8[e.e]]));
      }
      list.sort((a, b) => a.c.left - b.c.left || a.c.s - b.c.s);
      line.glyphs = list;
      return list;
    },

    /* An element drawing a layout (line < 0) or glyphs [first, end) of one line, its layout box at (x, y). */
    render(handle, x, y, color, lineIndex, first, end) {
      const L = GRText.compute(GRText.layouts.get(handle));
      const root = document.createElement('div');
      Object.assign(root.style, { position: 'absolute', left: '0', top: '0', transform: `translate(${x}px, ${y}px)` });
      const lines = lineIndex < 0 ? L.lines : [L.lines[lineIndex]];
      for (const line of lines) {
        // Glyph ranges draw the whole line, exactly as the layout draws it but without decorations, clipped to the
        // chosen glyphs' ink (half a point wider for anti-aliasing) over three line heights. Ink, not advances: a
        // neighbor's overhang (W into g) must stay out.
        const { element } = GRText.lineElement(L, line, { color, decorations: lineIndex < 0 });
        Object.assign(element.style, { position: 'absolute', left: line.x + 'px', top: line.top + 'px' });
        if (lineIndex >= 0) {
          const h = line.height;
          const boxes = GRText.glyphs(L, line).slice(first, end).filter(({ values: v }) => v[5] > 0).map(({ values: v }) => {
            const left = v[3] - 0.5 - line.x, right = v[3] + v[5] + 0.5 - line.x;
            return `M${left} ${-h}H${right}V${2 * h}H${left}Z`;
          });
          element.style.clipPath = `path('${boxes.join('') || 'M0 0Z'}')`;
        }
        root.append(element);
      }
      return root;
    },

    get(handle) {
      return GRText.compute(GRText.layouts.get(handle));
    },
  },

  gr_web_js_text_new__deps: ['$GRText'],
  gr_web_js_text_new: (utf8, length, operands, count) => {
    const handle = GRText.next++;
    GRText.layouts.set(handle, GRText.create(utf8, length, operands, count));
    return handle;
  },

  gr_web_js_text_free__deps: ['$GRText'],
  gr_web_js_text_free: (handle) => {
    GRText.layouts.delete(handle);
  },

  gr_web_js_text_metrics__deps: ['$GRText'],
  gr_web_js_text_metrics: (handle, out) => {
    const L = GRText.get(handle), first = L.lines[0], last = L.lines[L.lines.length - 1];
    HEAPF64.set([L.width, L.height, L.lines.length, first.baseline, last.baseline, L.u8[L.visibleEnd]], out >> 3);
  },

  gr_web_js_text_truncated__deps: ['$GRText'],
  gr_web_js_text_truncated: (handle) => GRText.get(handle).truncated,

  gr_web_js_text_line__deps: ['$GRText'],
  gr_web_js_text_line: (handle, index, out) => {
    const L = GRText.get(handle), l = L.lines[index];
    HEAPF64.set([L.u8[l.start], L.u8[l.end], l.x, l.top, l.width, l.height, l.baseline, l.baseline - l.top,
      l.top + l.height - l.baseline], out >> 3);
  },

  gr_web_js_text_caret_stop__deps: ['$GRText'],
  gr_web_js_text_caret_stop: (handle, index, next) => {
    const b = GRText.get(handle).boundaries8;
    if (next) return b.find((x) => x > index) ?? b[b.length - 1];
    return b.findLast((x) => x < index) ?? 0;
  },

  gr_web_js_text_hit_test__deps: ['$GRText'],
  gr_web_js_text_hit_test: (handle, x, y, out) => {
    const L = GRText.get(handle);
    const [index, affinity] = GRText.hitTest(L, x, y);
    HEAPF64.set([L.u8[index], affinity], out >> 3);
  },

  gr_web_js_text_caret__deps: ['$GRText'],
  gr_web_js_text_caret: (handle, index8, affinity, out) => {
    const L = GRText.get(handle), index = L.toU16(index8), upstream = affinity === 1;
    const line = GRText.lineFor(L, index, upstream);
    HEAPF64.set([GRText.caretX(L, line, index, upstream), line.top, 0, line.height], out >> 3);
  },

  gr_web_js_text_range_rects__deps: ['$GRText'],
  gr_web_js_text_range_rects: (handle, start, end, out, capacity) => {
    const L = GRText.get(handle);
    const rects = GRText.rangeRects(L, L.toU16(start), L.toU16(end));
    for (let k = 0; k < Math.min(capacity, rects.length); k++) HEAPF64.set(rects[k], (out >> 3) + k * 4);
    return rects.length;
  },

  gr_web_js_text_glyphs__deps: ['$GRText'],
  gr_web_js_text_glyphs: (handle, index, out, capacity) => {
    const L = GRText.get(handle), glyphs = GRText.glyphs(L, L.lines[index]);
    for (let k = 0; k < Math.min(capacity, glyphs.length); k++) HEAPF64.set(glyphs[k].values, (out >> 3) + k * 13);
    return glyphs.length;
  },
});
