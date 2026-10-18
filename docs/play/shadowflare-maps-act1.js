
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
      var PACKAGE_NAME = '/home/user/shadowflare/build-web-site/site/shadowflare-maps-act1.data';
      var REMOTE_PACKAGE_BASE = 'shadowflare-maps-act1.data';
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
Module['FS_createPath']("/content", "Map", true, true);
Module['FS_createPath']("/content/Map", "Overview", true, true);

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
          Module['removeRunDependency']('datafile_/home/user/shadowflare/build-web-site/site/shadowflare-maps-act1.data');
      }
      Module['addRunDependency']('datafile_/home/user/shadowflare/build-web-site/site/shadowflare-maps-act1.data');

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
    loadPackage({"files": [{"filename": "/content/Map/Overview/d01_01.png", "start": 0, "end": 56363}, {"filename": "/content/Map/Overview/d01_02.png", "start": 56363, "end": 100344}, {"filename": "/content/Map/Overview/d01_03.png", "start": 100344, "end": 124423}, {"filename": "/content/Map/Overview/d01_04.png", "start": 124423, "end": 209343}, {"filename": "/content/Map/Overview/d01_05.png", "start": 209343, "end": 224088}, {"filename": "/content/Map/Overview/d01_06.png", "start": 224088, "end": 251474}, {"filename": "/content/Map/Overview/d02_00.png", "start": 251474, "end": 294572}, {"filename": "/content/Map/Overview/d02_01.png", "start": 294572, "end": 340095}, {"filename": "/content/Map/Overview/d02_02.png", "start": 340095, "end": 357859}, {"filename": "/content/Map/Overview/d03_00.png", "start": 357859, "end": 401925}, {"filename": "/content/Map/Overview/d03_01.png", "start": 401925, "end": 439695}, {"filename": "/content/Map/Overview/d03_02.png", "start": 439695, "end": 457700}, {"filename": "/content/Map/Overview/d04_00.png", "start": 457700, "end": 497252}, {"filename": "/content/Map/Overview/d04_01.png", "start": 497252, "end": 570724}, {"filename": "/content/Map/Overview/d04_02.png", "start": 570724, "end": 595213}, {"filename": "/content/Map/Overview/d05_00.png", "start": 595213, "end": 632424}, {"filename": "/content/Map/Overview/d05_01.png", "start": 632424, "end": 667157}, {"filename": "/content/Map/Overview/d05_02.png", "start": 667157, "end": 692823}, {"filename": "/content/Map/Overview/f00_01.png", "start": 692823, "end": 727715}, {"filename": "/content/Map/Overview/f00_02.png", "start": 727715, "end": 877149}, {"filename": "/content/Map/Overview/f00_04.png", "start": 877149, "end": 968158}, {"filename": "/content/Map/Overview/f00_05.png", "start": 968158, "end": 1010260}, {"filename": "/content/Map/Overview/f00_06.png", "start": 1010260, "end": 1121314}, {"filename": "/content/Map/Overview/f00_07.png", "start": 1121314, "end": 1211852}, {"filename": "/content/Map/Overview/f01_00.png", "start": 1211852, "end": 1260802}, {"filename": "/content/Map/Overview/f01_01.png", "start": 1260802, "end": 1458116}, {"filename": "/content/Map/Overview/f01_02.png", "start": 1458116, "end": 1550399}, {"filename": "/content/Map/Overview/f01_03.png", "start": 1550399, "end": 1652427}, {"filename": "/content/Map/Overview/f01_04.png", "start": 1652427, "end": 1754847}, {"filename": "/content/Map/d01_01.txt", "start": 1754847, "end": 1805210}, {"filename": "/content/Map/d01_02.txt", "start": 1805210, "end": 1845793}, {"filename": "/content/Map/d01_03.txt", "start": 1845793, "end": 1864486}, {"filename": "/content/Map/d01_04.txt", "start": 1864486, "end": 1972643}, {"filename": "/content/Map/d01_05.txt", "start": 1972643, "end": 1983748}, {"filename": "/content/Map/d01_06.txt", "start": 1983748, "end": 2004977}, {"filename": "/content/Map/d02_00.txt", "start": 2004977, "end": 2044195}, {"filename": "/content/Map/d02_01.txt", "start": 2044195, "end": 2094126}, {"filename": "/content/Map/d02_02.txt", "start": 2094126, "end": 2109437}, {"filename": "/content/Map/d03_00.txt", "start": 2109437, "end": 2148526}, {"filename": "/content/Map/d03_01.txt", "start": 2148526, "end": 2181034}, {"filename": "/content/Map/d03_02.txt", "start": 2181034, "end": 2196722}, {"filename": "/content/Map/d04_00.txt", "start": 2196722, "end": 2231757}, {"filename": "/content/Map/d04_01.txt", "start": 2231757, "end": 2379856}, {"filename": "/content/Map/d04_02.txt", "start": 2379856, "end": 2405547}, {"filename": "/content/Map/d05_00.txt", "start": 2405547, "end": 2439106}, {"filename": "/content/Map/d05_01.txt", "start": 2439106, "end": 2465988}, {"filename": "/content/Map/d05_02.txt", "start": 2465988, "end": 2498928}, {"filename": "/content/Map/f00_01.txt", "start": 2498928, "end": 2517182}, {"filename": "/content/Map/f00_02.txt", "start": 2517182, "end": 2627825}, {"filename": "/content/Map/f00_04.txt", "start": 2627825, "end": 2687600}, {"filename": "/content/Map/f00_05.txt", "start": 2687600, "end": 2715353}, {"filename": "/content/Map/f00_06.txt", "start": 2715353, "end": 2795155}, {"filename": "/content/Map/f00_07.txt", "start": 2795155, "end": 2857420}, {"filename": "/content/Map/f01_00.txt", "start": 2857420, "end": 2885490}, {"filename": "/content/Map/f01_01.txt", "start": 2885490, "end": 3065847}, {"filename": "/content/Map/f01_02.txt", "start": 3065847, "end": 3130500}, {"filename": "/content/Map/f01_03.txt", "start": 3130500, "end": 3199258}, {"filename": "/content/Map/f01_04.txt", "start": 3199258, "end": 3278982}], "remote_package_size": 3278982});

  })();
