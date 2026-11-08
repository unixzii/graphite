// The page side of the Web conformance harness (conformance/web.c), linked with --js-library and built with JSPI.
// tests/web/run.mjs provides graphiteSetScale, graphiteCapture and graphiteSaveFile on the page.

addToLibrary({
  $GRConf: { containers: new Map(), next: 1 },

  // Changes devicePixelRatio through the runner and waits for a frame at the new scale.
  web_conf_set_scale__async: true,
  web_conf_set_scale: async (scale) => {
    if (devicePixelRatio !== scale) {
      await graphiteSetScale(scale);
      await new Promise(requestAnimationFrame);
    }
    return devicePixelRatio === scale ? 1 : 0;
  },

  // A 100 x 100 point container at the page origin, registered as "!conf<n>".
  web_conf_container__deps: ['$GRConf', '$specialHTMLTargets'],
  web_conf_container: () => {
    const n = GRConf.next++;
    const element = document.createElement('div');
    Object.assign(element.style, { position: 'fixed', left: '0', top: '0', width: '100px', height: '100px' });
    document.body.append(element);
    GRConf.containers.set(n, element);
    specialHTMLTargets['!conf' + n] = element;
    return n;
  },

  web_conf_remove__deps: ['$GRConf', '$specialHTMLTargets'],
  web_conf_remove: (n) => {
    GRConf.containers.get(n)?.remove();
    GRConf.containers.delete(n);
    delete specialHTMLTargets['!conf' + n];
  },

  // Shows only container n and captures it as premultiplied RGBA8 into a malloc'd buffer in *out
  // (gr_conf_pixels: width, height, data).
  web_conf_capture__async: true,
  web_conf_capture__deps: ['$GRConf', 'malloc'],
  web_conf_capture: async (n, out) => {
    for (const [k, element] of GRConf.containers) element.style.display = k === n ? '' : 'none';
    await new Promise(requestAnimationFrame);
    const shot = await graphiteCapture(0, 0, 100, 100);
    if (!shot) return 0;
    const bytes = Uint8Array.from(atob(shot.data), (c) => c.charCodeAt(0));
    const data = _malloc(bytes.length);
    HEAPU8.set(bytes, data);
    HEAPU32[out >> 2] = shot.width;
    HEAPU32[(out >> 2) + 1] = shot.height;
    HEAPU32[(out >> 2) + 2] = data;
    return 1;
  },

  // Hands every file under directory to the runner, which writes it to the real output directory.
  web_conf_export__async: true,
  web_conf_export__deps: ['$FS', '$UTF8ToString'],
  web_conf_export: async (directory) => {
    const root = UTF8ToString(directory);
    const walk = async (path, relative) => {
      for (const name of FS.readdir(path)) {
        if (name === '.' || name === '..') continue;
        const full = path + '/' + name, rel = relative ? relative + '/' + name : name;
        if (FS.isDir(FS.stat(full).mode)) await walk(full, rel);
        else {
          const bytes = FS.readFile(full);
          let text = '';
          for (let i = 0; i < bytes.length; i += 0x8000) text += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
          await graphiteSaveFile(rel, btoa(text));
        }
      }
    };
    try {
      FS.stat(root);
    } catch {
      return;
    }
    await walk(root, '');
  },
});
