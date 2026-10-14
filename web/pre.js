// The browser side of the web build (linked in with --pre-js, so it sees
// the engine's FS). Saves, options and the area map's pictures live in
// /libsdl, SDL's settings folder, and the player's own ShadowFlare files in
// /originals. Both are kept in the browser's IndexedDB; nothing is sent
// anywhere.
Module.preRun = Module.preRun || [];
Module.preRun.push(function () {
  for (const dir of ['/libsdl', '/originals']) {
    FS.mkdir(dir);
    FS.mount(IDBFS, {}, dir);
  }
  // Saves are loaded before the game starts; the original files only when
  // the player asks for them (they are a few hundred MB).
  addRunDependency('sf-saves');
  syncMount('/libsdl', true).finally(function () { removeRunDependency('sf-saves'); });
});

// IndexedDB <-> one mount only, so saving the game never touches the
// original files and the other way round.
function syncMount(path, load) {
  return new Promise(function (resolve, reject) {
    const mount = FS.lookupPath(path).node.mount;
    mount.type.syncfs(mount, load, function (err) {
      if (err) {
        console.warn('Could not ' + (load ? 'load ' : 'keep ') + path + ':', err);
        reject(err);
      } else {
        resolve();
      }
    });
  });
}

const kSavedFlag = 'shadowflare-originals-saved';
let savesBusy = false;

Module.sf = {
  // Called by the engine every few seconds and when it quits.
  syncSaves: function () {
    if (savesBusy) return Promise.resolve();
    savesBusy = true;
    return syncMount('/libsdl', false).catch(function () {}).finally(function () { savesBusy = false; });
  },

  hasSavedOriginals: function () {
    try { return localStorage.getItem(kSavedFlag) === '1'; } catch (e) { return false; }
  },

  loadSavedOriginals: function () {
    return syncMount('/originals', true).then(function () {
      return FS.readdir('/originals').length > 2;
    });
  },

  forgetOriginals: function () {
    try { localStorage.removeItem(kSavedFlag); } catch (e) {}
    return new Promise(function (resolve) {
      const request = indexedDB.deleteDatabase('/originals');
      request.onsuccess = request.onerror = request.onblocked = function () { resolve(); };
    });
  },

  // The folder the player picked (an <input webkitdirectory> FileList):
  // finds the game in it (the folder holding Map and System), copies that
  // into /originals and, if asked, keeps it in the browser. Returns false
  // when there is no game in it.
  useOriginals: async function (files, keep, progress) {
    files = Array.from(files);
    const root = findGameRoot(files);
    if (root === null) return false;
    // Everything in the game's folder, ShadowFlare.exe too: the item
    // tables are read from it.
    const wanted = files.filter(function (f) { return (f.webkitRelativePath || f.name).startsWith(root); });
    const total = wanted.reduce(function (sum, f) { return sum + f.size; }, 0);
    let done = 0;
    for (const file of wanted) {
      const parts = (file.webkitRelativePath || file.name).slice(root.length).split('/');
      let dir = '/originals';
      for (const part of parts.slice(0, -1)) {
        dir += '/' + part;
        if (!FS.analyzePath(dir).exists) FS.mkdir(dir);
      }
      FS.writeFile(dir + '/' + parts[parts.length - 1], new Uint8Array(await file.arrayBuffer()));
      done += file.size;
      progress(done, total);
    }
    if (keep) {
      progress(total, total, true);
      await syncMount('/originals', false);
      try { localStorage.setItem(kSavedFlag, '1'); } catch (e) {}
    }
    return true;
  },

  start: function (args) {
    Module.callMain(args);
  },
};

// The shortest folder in the picked list with Map and System folders in
// it (any case), as a prefix ending in '/'; null when there is none.
function findGameRoot(files) {
  const dirs = new Set();
  for (const f of files) {
    const parts = (f.webkitRelativePath || f.name).split('/');
    for (let i = 1; i < parts.length; ++i) dirs.add(parts.slice(0, i).join('/').toLowerCase());
  }
  let best = null;
  for (const dir of dirs) {
    if (dirs.has(dir + '/map') && dirs.has(dir + '/system') && (best === null || dir.length < best.length)) best = dir;
  }
  if (best === null) return null;
  // Back to the folder's own spelling.
  for (const f of files) {
    const path = f.webkitRelativePath || f.name;
    if (path.toLowerCase().startsWith(best + '/')) return path.slice(0, best.length + 1);
  }
  return null;
}
