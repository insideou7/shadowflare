
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
      var PACKAGE_NAME = '/home/user/shadowflare/build-web-site/site/shadowflare-enemies-act4.data';
      var REMOTE_PACKAGE_BASE = 'shadowflare-enemies-act4.data';
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
Module['FS_createPath']("/content/Character/ENEMY", "00000019", true, true);
Module['FS_createPath']("/content/Character/ENEMY", "00000020", true, true);
Module['FS_createPath']("/content/Character/ENEMY", "00000021", true, true);
Module['FS_createPath']("/content/Character/ENEMY", "00000022", true, true);
Module['FS_createPath']("/content/Character/ENEMY", "00000023", true, true);
Module['FS_createPath']("/content/Character/ENEMY", "00000024", true, true);

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
          Module['removeRunDependency']('datafile_/home/user/shadowflare/build-web-site/site/shadowflare-enemies-act4.data');
      }
      Module['addRunDependency']('datafile_/home/user/shadowflare/build-web-site/site/shadowflare-enemies-act4.data');

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
    loadPackage({"files": [{"filename": "/content/Character/ENEMY/00000019/Animation.anim", "start": 0, "end": 4295}, {"filename": "/content/Character/ENEMY/00000019/Animation.png", "start": 4295, "end": 381900}, {"filename": "/content/Character/ENEMY/00000019/Animation.sheet", "start": 381900, "end": 391142}, {"filename": "/content/Character/ENEMY/00000020/Animation.anim", "start": 391142, "end": 395437}, {"filename": "/content/Character/ENEMY/00000020/Animation.png", "start": 395437, "end": 1640874}, {"filename": "/content/Character/ENEMY/00000020/Animation.sheet", "start": 1640874, "end": 1651054}, {"filename": "/content/Character/ENEMY/00000021/Animation.anim", "start": 1651054, "end": 1656107}, {"filename": "/content/Character/ENEMY/00000021/Animation.png", "start": 1656107, "end": 2039232}, {"filename": "/content/Character/ENEMY/00000021/Animation.sheet", "start": 2039232, "end": 2051086}, {"filename": "/content/Character/ENEMY/00000022/Animation.anim", "start": 2051086, "end": 2055819}, {"filename": "/content/Character/ENEMY/00000022/Animation.png", "start": 2055819, "end": 3006183}, {"filename": "/content/Character/ENEMY/00000022/Animation.sheet", "start": 3006183, "end": 3019069}, {"filename": "/content/Character/ENEMY/00000023/Animation.anim", "start": 3019069, "end": 3024186}, {"filename": "/content/Character/ENEMY/00000023/Animation.png", "start": 3024186, "end": 4307275}, {"filename": "/content/Character/ENEMY/00000023/Animation.sheet", "start": 4307275, "end": 4320253}, {"filename": "/content/Character/ENEMY/00000024/Animation.anim", "start": 4320253, "end": 4324986}, {"filename": "/content/Character/ENEMY/00000024/Animation.png", "start": 4324986, "end": 10747713}, {"filename": "/content/Character/ENEMY/00000024/Animation.sheet", "start": 10747713, "end": 10761642}], "remote_package_size": 10761642});

  })();
