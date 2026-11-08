// Page helpers for the Web example shell (web.c), linked with --js-library.
addToLibrary({
  example_web_prepare__deps: ['$UTF8ToString'],
  example_web_prepare: (title, width, height) => {
    document.title = UTF8ToString(title);
    if (document.getElementById('graphite')) return;
    const element = document.createElement('div');
    element.id = 'graphite';
    Object.assign(element.style, { width: width + 'px', height: height + 'px' });
    document.body.append(element);
  },
});
