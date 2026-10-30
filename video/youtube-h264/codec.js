(() => {
  const blocked = /\b(?:av01|vp0?9|vp0?8)(?:\b|\.)/i;
  const wrap = (owner, name, unsupported) => {
    const original = owner?.[name];
    if (typeof original !== 'function') return;
    Object.defineProperty(owner, name, {
      configurable: true, writable: true,
      value: function (...args) {
        return blocked.test(String(args[0])) ? unsupported : Reflect.apply(original, this, args);
      }
    });
  };
  wrap(globalThis.MediaSource, 'isTypeSupported', false);
  wrap(globalThis.ManagedMediaSource, 'isTypeSupported', false);
  wrap(HTMLMediaElement.prototype, 'canPlayType', '');
})();
