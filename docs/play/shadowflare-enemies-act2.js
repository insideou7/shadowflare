
  var Module = typeof Module != 'undefined' ? Module : {};

  if (!Module['expectedDataFileDownloads']) Module['expectedDataFileDownloads'] = 0;
  Module['expectedDataFileDownloads']++;
  (() => {
    // Do not attempt to redownload the virtual filesystem data when in a pthread or a Wasm Worker context.
    var isPthread = typeof ENVIRONMENT_IS_PTHREAD != 'undefined' && ENVIRONMENT_IS_PTHREAD;
    var isWasmWorker = typeof ENVIRONMENT_IS_WASM_WORKER != 'undefined' && ENVIRONMENT_IS_WASM_WORKER;
    if (isPthread || isWasmWorker) return;
    async function loadPackage(metadata) {

      var PACKAGE_PATH = '';
      if (typeof window === 'object') {
        PACKAGE_PATH = window['encodeURIComponent'](window.location.pathname.substring(0, window.location.pathname.lastIndexOf('/')) + '/');
      } else if (typeof process === 'undefined' && typeof location !== 'undefined') {
        // web worker
        PACKAGE_PATH = encodeURIComponent(location.pathname.substring(0, location.pathname.lastIndexOf('/')) + '/');
      }
      var PACKAGE_NAME = '/home/user/shadowflare/build-web-site/site/shadowflare-enemies-act2.data';
      var REMOTE_PACKAGE_BASE = 'shadowflare-enemies-act2.data';
      var REMOTE_PACKAGE_NAME = Module['locateFile'] ? Module['locateFile'](REMOTE_PACKAGE_BASE, '') : REMOTE_PACKAGE_BASE;
      var REMOTE_PACKAGE_SIZE = metadata['remote_package_size'];

      async function fetchRemotePackage(packageName, packageSize) {
        
        if (!Module['dataFileDownloads']) Module['dataFileDownloads'] = {};
        try {
          var response = await fetch(packageName);
        } catch (e) {
          throw new Error(`Network Error: ${packageName}`, {e});
        }
        if (!response.ok) {
          throw new Error(`${response.status}: ${response.url}`);
        }

        const chunks = [];
        const headers = response.headers;
        const total = Number(headers.get('Content-Length') || packageSize);
        let loaded = 0;

        Module['setStatus'] && Module['setStatus']('Downloading data...');
        const reader = response.body.getReader();

        while (1) {
          var {done, value} = await reader.read();
          if (done) break;
          chunks.push(value);
          loaded += value.length;
          Module['dataFileDownloads'][packageName] = {loaded, total};

          let totalLoaded = 0;
          let totalSize = 0;

          for (const download of Object.values(Module['dataFileDownloads'])) {
            totalLoaded += download.loaded;
            totalSize += download.total;
          }

          Module['setStatus'] && Module['setStatus'](`Downloading data... (${totalLoaded}/${totalSize})`);
        }

        const packageData = new Uint8Array(chunks.map((c) => c.length).reduce((a, b) => a + b, 0));
        let offset = 0;
        for (const chunk of chunks) {
          packageData.set(chunk, offset);
          offset += chunk.length;
        }
        return packageData.buffer;
      }

      var fetchPromise;
      var fetched = Module['getPreloadedPackage'] && Module['getPreloadedPackage'](REMOTE_PACKAGE_NAME, REMOTE_PACKAGE_SIZE);

      if (!fetched) {
        // Note that we don't use await here because we want to execute the
        // the rest of this function immediately.
        fetchPromise = fetchRemotePackage(REMOTE_PACKAGE_NAME, REMOTE_PACKAGE_SIZE);
      }

    async function runWithFS(Module) {

      function assert(check, msg) {
        if (!check) throw new Error(msg);
      }
Module['FS_createPath']("/", "content", true, true);
Module['FS_createPath']("/content", "Character", true, true);
Module['FS_createPath']("/content/Character", "ENEMY", true, true);
Module['FS_createPath']("/content/Character/ENEMY", "00000006", true, true);
Module['FS_createPath']("/content/Character/ENEMY", "00000007", true, true);
Module['FS_createPath']("/content/Character/ENEMY", "00000008", true, true);
Module['FS_createPath']("/content/Character/ENEMY", "00000009", true, true);
Module['FS_createPath']("/content/Character/ENEMY", "00000010", true, true);
Module['FS_createPath']("/content/Character/ENEMY", "00000011", true, true);
Module['FS_createPath']("/content/Character/ENEMY", "00000012", true, true);
Module['FS_createPath']("/content/Character/ENEMY", "00000013", true, true);

      async function processPackageData(arrayBuffer) {
        assert(arrayBuffer, 'Loading data file failed.');
        assert(arrayBuffer.constructor.name === ArrayBuffer.name, 'bad input to processPackageData ' + arrayBuffer.constructor.name);
        var byteArray = new Uint8Array(arrayBuffer);
        var curr;
        // Reuse the bytearray from the XHR as the source for file reads.
          for (var file of metadata['files']) {
            var name = file['filename'];
            var data = byteArray.subarray(file['start'], file['end']);
            // canOwn this data in the filesystem, it is a slice into the heap that will never change
        Module['FS_createDataFile'](name, null, data, true, true, true);
          }
          Module['removeRunDependency']('datafile_/home/user/shadowflare/build-web-site/site/shadowflare-enemies-act2.data');
      }
      Module['addRunDependency']('datafile_/home/user/shadowflare/build-web-site/site/shadowflare-enemies-act2.data');

      if (!Module['preloadResults']) Module['preloadResults'] = {};

      Module['preloadResults'][PACKAGE_NAME] = {fromCache: false};
      if (!fetched) {
        fetched = await fetchPromise;
      }
      await processPackageData(fetched);

    }
    // Detect whether the module JS file has already been loaded.
    if (Module['FS_createPath']) {
      runWithFS(Module);
    } else {
      if (!Module['preRun']) Module['preRun'] = [];
      Module['preRun'].push(runWithFS); // FS is not initialized yet, wait for it
    }

    }
    loadPackage({"files": [{"filename": "/content/Character/ENEMY/00000006/Animation.anim", "start": 0, "end": 6461}, {"filename": "/content/Character/ENEMY/00000006/Animation.png", "start": 6461, "end": 517813}, {"filename": "/content/Character/ENEMY/00000006/Animation.sheet", "start": 517813, "end": 529598}, {"filename": "/content/Character/ENEMY/00000007/Animation.anim", "start": 529598, "end": 536059}, {"filename": "/content/Character/ENEMY/00000007/Animation.png", "start": 536059, "end": 1604985}, {"filename": "/content/Character/ENEMY/00000007/Animation.sheet", "start": 1604985, "end": 1617553}, {"filename": "/content/Character/ENEMY/00000008/Animation.anim", "start": 1617553, "end": 1621080}, {"filename": "/content/Character/ENEMY/00000008/Animation.png", "start": 1621080, "end": 2807351}, {"filename": "/content/Character/ENEMY/00000008/Animation.sheet", "start": 2807351, "end": 2817600}, {"filename": "/content/Character/ENEMY/00000009/Animation.anim", "start": 2817600, "end": 2821223}, {"filename": "/content/Character/ENEMY/00000009/Animation.png", "start": 2821223, "end": 3514326}, {"filename": "/content/Character/ENEMY/00000009/Animation.sheet", "start": 3514326, "end": 3524004}, {"filename": "/content/Character/ENEMY/00000010/Animation.anim", "start": 3524004, "end": 3528075}, {"filename": "/content/Character/ENEMY/00000010/Animation.png", "start": 3528075, "end": 3809340}, {"filename": "/content/Character/ENEMY/00000010/Animation.sheet", "start": 3809340, "end": 3818549}, {"filename": "/content/Character/ENEMY/00000011/Animation.anim", "start": 3818549, "end": 3822011}, {"filename": "/content/Character/ENEMY/00000011/Animation.png", "start": 3822011, "end": 4128565}, {"filename": "/content/Character/ENEMY/00000011/Animation.sheet", "start": 4128565, "end": 4137864}, {"filename": "/content/Character/ENEMY/00000012/Animation.anim", "start": 4137864, "end": 4143893}, {"filename": "/content/Character/ENEMY/00000012/Animation.png", "start": 4143893, "end": 9095770}, {"filename": "/content/Character/ENEMY/00000012/Animation.sheet", "start": 9095770, "end": 9109791}, {"filename": "/content/Character/ENEMY/00000013/Animation.anim", "start": 9109791, "end": 9113414}, {"filename": "/content/Character/ENEMY/00000013/Animation.png", "start": 9113414, "end": 9701512}, {"filename": "/content/Character/ENEMY/00000013/Animation.sheet", "start": 9701512, "end": 9710890}], "remote_package_size": 9710890});

  })();
