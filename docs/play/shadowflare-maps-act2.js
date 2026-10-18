
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
      var PACKAGE_NAME = '/home/user/shadowflare/build-web-site/site/shadowflare-maps-act2.data';
      var REMOTE_PACKAGE_BASE = 'shadowflare-maps-act2.data';
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
          Module['removeRunDependency']('datafile_/home/user/shadowflare/build-web-site/site/shadowflare-maps-act2.data');
      }
      Module['addRunDependency']('datafile_/home/user/shadowflare/build-web-site/site/shadowflare-maps-act2.data');

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
    loadPackage({"files": [{"filename": "/content/Map/Coalmine_01.txt", "start": 0, "end": 52102}, {"filename": "/content/Map/Coalmine_02.txt", "start": 52102, "end": 87719}, {"filename": "/content/Map/Coalmine_03.txt", "start": 87719, "end": 140326}, {"filename": "/content/Map/Coalmine_04.txt", "start": 140326, "end": 185735}, {"filename": "/content/Map/Overview/Coalmine_01.png", "start": 185735, "end": 237683}, {"filename": "/content/Map/Overview/Coalmine_02.png", "start": 237683, "end": 276405}, {"filename": "/content/Map/Overview/Coalmine_03.png", "start": 276405, "end": 332482}, {"filename": "/content/Map/Overview/Coalmine_04.png", "start": 332482, "end": 376469}, {"filename": "/content/Map/Overview/c02_00.png", "start": 376469, "end": 429048}, {"filename": "/content/Map/Overview/c02_01.png", "start": 429048, "end": 499019}, {"filename": "/content/Map/Overview/cave_01.png", "start": 499019, "end": 543058}, {"filename": "/content/Map/Overview/cave_02.png", "start": 543058, "end": 565601}, {"filename": "/content/Map/Overview/cave_03.png", "start": 565601, "end": 593359}, {"filename": "/content/Map/Overview/cave_04.png", "start": 593359, "end": 632803}, {"filename": "/content/Map/Overview/cave_05.png", "start": 632803, "end": 703510}, {"filename": "/content/Map/Overview/cave_06.png", "start": 703510, "end": 794578}, {"filename": "/content/Map/Overview/cave_07.png", "start": 794578, "end": 865727}, {"filename": "/content/Map/Overview/cave_08.png", "start": 865727, "end": 908822}, {"filename": "/content/Map/Overview/cave_09.png", "start": 908822, "end": 970547}, {"filename": "/content/Map/Overview/cave_10.png", "start": 970547, "end": 997716}, {"filename": "/content/Map/Overview/f02_00_00.png", "start": 997716, "end": 1428722}, {"filename": "/content/Map/Overview/f02_00_01.png", "start": 1428722, "end": 1603964}, {"filename": "/content/Map/Overview/f02_01_00.png", "start": 1603964, "end": 1849353}, {"filename": "/content/Map/Overview/f02_01_01.png", "start": 1849353, "end": 2041772}, {"filename": "/content/Map/Overview/f02_01_02.png", "start": 2041772, "end": 2218294}, {"filename": "/content/Map/Overview/f02_01_03.png", "start": 2218294, "end": 2296589}, {"filename": "/content/Map/Overview/f02_01_04.png", "start": 2296589, "end": 2473269}, {"filename": "/content/Map/Overview/f02_01_05.png", "start": 2473269, "end": 2656971}, {"filename": "/content/Map/Overview/f02_02_00.png", "start": 2656971, "end": 2897756}, {"filename": "/content/Map/Overview/f02_02_01.png", "start": 2897756, "end": 2945357}, {"filename": "/content/Map/Overview/f02_02_02.png", "start": 2945357, "end": 3220240}, {"filename": "/content/Map/Overview/f02_02_03.png", "start": 3220240, "end": 3381284}, {"filename": "/content/Map/Overview/f02_02_04.png", "start": 3381284, "end": 3544016}, {"filename": "/content/Map/Overview/f02_99_99.png", "start": 3544016, "end": 3564477}, {"filename": "/content/Map/Overview/fort_001.png", "start": 3564477, "end": 3606507}, {"filename": "/content/Map/Overview/fort_002.png", "start": 3606507, "end": 3656966}, {"filename": "/content/Map/Overview/fort_003.png", "start": 3656966, "end": 3723718}, {"filename": "/content/Map/c02_00.txt", "start": 3723718, "end": 3756722}, {"filename": "/content/Map/c02_01.txt", "start": 3756722, "end": 3805358}, {"filename": "/content/Map/cave_01.txt", "start": 3805358, "end": 3853147}, {"filename": "/content/Map/cave_02.txt", "start": 3853147, "end": 3876678}, {"filename": "/content/Map/cave_03.txt", "start": 3876678, "end": 3905439}, {"filename": "/content/Map/cave_04.txt", "start": 3905439, "end": 3953660}, {"filename": "/content/Map/cave_05.txt", "start": 3953660, "end": 4062526}, {"filename": "/content/Map/cave_06.txt", "start": 4062526, "end": 4207479}, {"filename": "/content/Map/cave_07.txt", "start": 4207479, "end": 4309859}, {"filename": "/content/Map/cave_08.txt", "start": 4309859, "end": 4357482}, {"filename": "/content/Map/cave_09.txt", "start": 4357482, "end": 4423501}, {"filename": "/content/Map/cave_10.txt", "start": 4423501, "end": 4452647}, {"filename": "/content/Map/f02_00_00.txt", "start": 4452647, "end": 4924763}, {"filename": "/content/Map/f02_00_01.txt", "start": 4924763, "end": 5062289}, {"filename": "/content/Map/f02_01_00.txt", "start": 5062289, "end": 5272697}, {"filename": "/content/Map/f02_01_01.txt", "start": 5272697, "end": 5447871}, {"filename": "/content/Map/f02_01_02.txt", "start": 5447871, "end": 5576539}, {"filename": "/content/Map/f02_01_03.txt", "start": 5576539, "end": 5634479}, {"filename": "/content/Map/f02_01_04.txt", "start": 5634479, "end": 5762701}, {"filename": "/content/Map/f02_01_05.txt", "start": 5762701, "end": 5889324}, {"filename": "/content/Map/f02_02_00.txt", "start": 5889324, "end": 6104112}, {"filename": "/content/Map/f02_02_01.txt", "start": 6104112, "end": 6133745}, {"filename": "/content/Map/f02_02_02.txt", "start": 6133745, "end": 6355871}, {"filename": "/content/Map/f02_02_03.txt", "start": 6355871, "end": 6457049}, {"filename": "/content/Map/f02_02_04.txt", "start": 6457049, "end": 6560548}, {"filename": "/content/Map/f02_99_99.txt", "start": 6560548, "end": 6572146}, {"filename": "/content/Map/fort_001.txt", "start": 6572146, "end": 6624218}, {"filename": "/content/Map/fort_002.txt", "start": 6624218, "end": 6674482}, {"filename": "/content/Map/fort_003.txt", "start": 6674482, "end": 6761314}], "remote_package_size": 6761314});

  })();
