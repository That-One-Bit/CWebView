
(function () {
  var MANIFEST = '/data/rooms_index.json';
  var FULL = '/data/gamedata_full.json';

  function isFnPlaceholder(o) {
    return o !== null && typeof o === 'object' &&
      Object.keys(o).length === 1 && o.__fn !== undefined;
  }

  function resolveFn(ph, path) {
    var fn = window[ph.__fn];
    if (typeof fn !== 'function')
      throw new Error(path + ': function not found on window: ' + ph.__fn +
        ' (edited JSON referencing a function that does not exist?)');
    return fn;
  }

  function overlay(dst, src, path) {
    if (isFnPlaceholder(src)) return resolveFn(src, path);

    if (Array.isArray(src)) {
      var arr = Array.isArray(dst) ? dst : [];
      for (var i = 0; i < src.length; i++) arr[i] = overlay(arr[i], src[i], path + '[' + i + ']');
      return arr;
    }
    if (src !== null && typeof src === 'object') {
      var out = (dst !== null && typeof dst === 'object' && !Array.isArray(dst))
        ? dst : {};
      for (var k in src) out[k] = overlay(out[k], src[k], path + '.' + k);
      return out;
    }
    return src;
  }

  function fetchJSON(url) {
    return fetch(url, { cache: 'no-store' }).then(function (r) {
      if (!r.ok) throw new Error('HTTP ' + r.status + ' for ' + url +
        ' — start the server with `python3 tools/serve.py debug` (or `dev`)');
      return r.json();
    }).then(function (j) {
      // serve.py reports a broken readable room file as {error: "..."}
      if (j && typeof j === 'object' && !Array.isArray(j) && typeof j.error === 'string' && Object.keys(j).length === 1)
        throw new Error(j.error);
      return j;
    });
  }

  var state = { merged: false, error: null, roomsApplied: [], stats: null };

  function applyFull(g, full) {
    for (var k in full)
      g[k] = overlay(g[k], full[k], k);
  }

  function applyRooms(g, rooms) {
    for (var i = 0; i < rooms.length; i++) {
      var room = rooms[i];
      if (!room || typeof room !== 'object' || !room.raw || typeof room.id !== 'number')
        throw new Error('rooms entry #' + i + ': not a room dump (missing id/raw)');
      if (!g.rooms[room.id])
        throw new Error('rooms entry ' + (room.name || '#' + i) +
          ': id ' + room.id + ' out of range (gamedata has ' + g.rooms.length + ' rooms)');
      g.rooms[room.id] = overlay(g.rooms[room.id], room.raw, 'rooms.' + room.id);
      state.roomsApplied.push((room.name || '?' + room.id));
    }
  }

  function boot(GameMaker_Init) {
    var badge = badgeEl();
    if (typeof window.fetch !== 'function') {
      return fail(badge, 'fetch() unavailable — open the game over http:// via tools/serve.py, not file://');
    }
    if (!window.gamedata) {
      return fail(badge, 'window.gamedata missing — debug_loader.js must load AFTER html5game/Lost.js');
    }
    fetchJSON(FULL).then(function (full) {
      applyFull(window.gamedata, full);

      return fetchJSON(MANIFEST).catch(function () { return null; }).then(function (list) {
        if (!list || !list.length) return [];
        return Promise.all(list.map(function (fn) {
          return fetchJSON('/data/rooms/' + fn);
        }));
      });
    }).then(function (rooms) {
      if (rooms && rooms.length) applyRooms(window.gamedata, rooms);
      var g = window.gamedata;
      state.stats = {
        rooms: g.rooms.length, sprites: g.sprites.length, rects: g.spriteRects.length,
        objects: g.objects.length, sounds: g.sounds.length,
        roomsFromFiles: state.roomsApplied.length
      };
      state.merged = true;
      setBadge(badge, 'DEBUG · gamedata from data/ · ' +
        state.stats.rooms + ' rooms (' + state.stats.roomsFromFiles + ' from data/rooms/) · ' +
        state.stats.sprites + ' sprites', '#1d7a3e');
      GameMaker_Init();
    }).catch(function (e) {
      fail(badge, e && e.message ? e.message : String(e));
    });
  }

  function badgeEl() {
    var el = document.getElementById('debug_badge');
    if (!el) {
      el = document.createElement('div');
      el.id = 'debug_badge';
      el.style.cssText = 'position:fixed;top:8px;right:8px;z-index:99999;' +
        'font:12px monospace;color:#fff;background:#666;padding:4px 8px;' +
        'border-radius:4px;max-width:70%;white-space:pre-wrap;';
      document.body.appendChild(el);
    }
    return el;
  }
  function setBadge(el, text, color) {
    el.textContent = text;
    el.style.background = color;
  }
  function fail(el, msg) {
    state.error = msg;
    setBadge(el, 'DEBUG FAILED — game NOT started\n' + msg + '\n' +
      'fix the JSON (data/gamedata_full.json or data/rooms/*.json) and refresh', '#8a1f1f');
    if (window.console && console.error) console.error('debug_loader: ' + msg);
  }

  window.DebugOrig = { boot: boot, state: state, _overlay: overlay };
})();
