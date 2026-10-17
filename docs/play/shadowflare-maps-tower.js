
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
      var PACKAGE_NAME = '/home/user/shadowflare/build-web-site/site/shadowflare-maps-tower.data';
      var REMOTE_PACKAGE_BASE = 'shadowflare-maps-tower.data';
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
          Module['removeRunDependency']('datafile_/home/user/shadowflare/build-web-site/site/shadowflare-maps-tower.data');
      }
      Module['addRunDependency']('datafile_/home/user/shadowflare/build-web-site/site/shadowflare-maps-tower.data');

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
    loadPackage({"files": [{"filename": "/content/Map/Overview/y00_00_19000000.png", "start": 0, "end": 8434}, {"filename": "/content/Map/Overview/y00_01.png", "start": 8434, "end": 16124}, {"filename": "/content/Map/Overview/y00_02.png", "start": 16124, "end": 33480}, {"filename": "/content/Map/Overview/y00_03.png", "start": 33480, "end": 55566}, {"filename": "/content/Map/Overview/y00_04.png", "start": 55566, "end": 83964}, {"filename": "/content/Map/Overview/y00_05.png", "start": 83964, "end": 112630}, {"filename": "/content/Map/Overview/y00_06.png", "start": 112630, "end": 155220}, {"filename": "/content/Map/Overview/y01_00.png", "start": 155220, "end": 168391}, {"filename": "/content/Map/Overview/y01_01_19000008.png", "start": 168391, "end": 181520}, {"filename": "/content/Map/Overview/y01_01_19000009.png", "start": 181520, "end": 194671}, {"filename": "/content/Map/Overview/y01_01_19000010.png", "start": 194671, "end": 207790}, {"filename": "/content/Map/Overview/y01_01_19000011.png", "start": 207790, "end": 220914}, {"filename": "/content/Map/Overview/y01_01_19000012.png", "start": 220914, "end": 233979}, {"filename": "/content/Map/Overview/y01_01_19000014.png", "start": 233979, "end": 247086}, {"filename": "/content/Map/Overview/y01_01_19000015.png", "start": 247086, "end": 260113}, {"filename": "/content/Map/Overview/y01_01_19000016.png", "start": 260113, "end": 273236}, {"filename": "/content/Map/Overview/y01_01_19000017.png", "start": 273236, "end": 286390}, {"filename": "/content/Map/Overview/y01_01_19000019.png", "start": 286390, "end": 299548}, {"filename": "/content/Map/Overview/y01_01_19000020.png", "start": 299548, "end": 312663}, {"filename": "/content/Map/Overview/y01_01_19000021.png", "start": 312663, "end": 325848}, {"filename": "/content/Map/Overview/y01_01_19000022.png", "start": 325848, "end": 338910}, {"filename": "/content/Map/Overview/y01_02.png", "start": 338910, "end": 351942}, {"filename": "/content/Map/Overview/y01_03.png", "start": 351942, "end": 365113}, {"filename": "/content/Map/Overview/y04_00.png", "start": 365113, "end": 373485}, {"filename": "/content/Map/Overview/y04_01.png", "start": 373485, "end": 394901}, {"filename": "/content/Map/Overview/y04_01_19000025.png", "start": 394901, "end": 417290}, {"filename": "/content/Map/Overview/y04_01_19000026.png", "start": 417290, "end": 439171}, {"filename": "/content/Map/Overview/y04_01_19000027.png", "start": 439171, "end": 461066}, {"filename": "/content/Map/Overview/y04_01_19000028.png", "start": 461066, "end": 482451}, {"filename": "/content/Map/Overview/y04_01_19000029.png", "start": 482451, "end": 504379}, {"filename": "/content/Map/Overview/y04_01_19000030.png", "start": 504379, "end": 526278}, {"filename": "/content/Map/Overview/y04_01_19000031.png", "start": 526278, "end": 548024}, {"filename": "/content/Map/Overview/y04_01_19000032.png", "start": 548024, "end": 569466}, {"filename": "/content/Map/Overview/y04_01_19000033.png", "start": 569466, "end": 591571}, {"filename": "/content/Map/Overview/y04_01_19000034.png", "start": 591571, "end": 613254}, {"filename": "/content/Map/Overview/y04_01_19000035.png", "start": 613254, "end": 634957}, {"filename": "/content/Map/Overview/y04_01_19000036.png", "start": 634957, "end": 656382}, {"filename": "/content/Map/Overview/y04_02.png", "start": 656382, "end": 664636}, {"filename": "/content/Map/y00_00_19000000.txt", "start": 664636, "end": 671185}, {"filename": "/content/Map/y00_01.txt", "start": 671185, "end": 677869}, {"filename": "/content/Map/y00_02.txt", "start": 677869, "end": 694741}, {"filename": "/content/Map/y00_03.txt", "start": 694741, "end": 716520}, {"filename": "/content/Map/y00_04.txt", "start": 716520, "end": 744212}, {"filename": "/content/Map/y00_05.txt", "start": 744212, "end": 772262}, {"filename": "/content/Map/y00_06.txt", "start": 772262, "end": 809502}, {"filename": "/content/Map/y01_00.txt", "start": 809502, "end": 820030}, {"filename": "/content/Map/y01_01_19000008.txt", "start": 820030, "end": 831217}, {"filename": "/content/Map/y01_01_19000009.txt", "start": 831217, "end": 842401}, {"filename": "/content/Map/y01_01_19000010.txt", "start": 842401, "end": 853595}, {"filename": "/content/Map/y01_01_19000011.txt", "start": 853595, "end": 864785}, {"filename": "/content/Map/y01_01_19000012.txt", "start": 864785, "end": 875974}, {"filename": "/content/Map/y01_01_19000014.txt", "start": 875974, "end": 887164}, {"filename": "/content/Map/y01_01_19000015.txt", "start": 887164, "end": 898357}, {"filename": "/content/Map/y01_01_19000016.txt", "start": 898357, "end": 909548}, {"filename": "/content/Map/y01_01_19000017.txt", "start": 909548, "end": 920741}, {"filename": "/content/Map/y01_01_19000019.txt", "start": 920741, "end": 931931}, {"filename": "/content/Map/y01_01_19000020.txt", "start": 931931, "end": 943123}, {"filename": "/content/Map/y01_01_19000021.txt", "start": 943123, "end": 954319}, {"filename": "/content/Map/y01_01_19000022.txt", "start": 954319, "end": 965513}, {"filename": "/content/Map/y01_02.txt", "start": 965513, "end": 976565}, {"filename": "/content/Map/y01_03.txt", "start": 976565, "end": 987722}, {"filename": "/content/Map/y04_00.txt", "start": 987722, "end": 994688}, {"filename": "/content/Map/y04_01.txt", "start": 994688, "end": 1014390}, {"filename": "/content/Map/y04_01_19000025.txt", "start": 1014390, "end": 1034090}, {"filename": "/content/Map/y04_01_19000026.txt", "start": 1034090, "end": 1053786}, {"filename": "/content/Map/y04_01_19000027.txt", "start": 1053786, "end": 1073477}, {"filename": "/content/Map/y04_01_19000028.txt", "start": 1073477, "end": 1093181}, {"filename": "/content/Map/y04_01_19000029.txt", "start": 1093181, "end": 1112883}, {"filename": "/content/Map/y04_01_19000030.txt", "start": 1112883, "end": 1132579}, {"filename": "/content/Map/y04_01_19000031.txt", "start": 1132579, "end": 1152282}, {"filename": "/content/Map/y04_01_19000032.txt", "start": 1152282, "end": 1171985}, {"filename": "/content/Map/y04_01_19000033.txt", "start": 1171985, "end": 1191682}, {"filename": "/content/Map/y04_01_19000034.txt", "start": 1191682, "end": 1211381}, {"filename": "/content/Map/y04_01_19000035.txt", "start": 1211381, "end": 1231081}, {"filename": "/content/Map/y04_01_19000036.txt", "start": 1231081, "end": 1250782}, {"filename": "/content/Map/y04_02.txt", "start": 1250782, "end": 1257462}], "remote_package_size": 1257462});

  })();
