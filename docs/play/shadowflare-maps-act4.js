
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
      var PACKAGE_NAME = '/home/user/shadowflare/build-web-site/site/shadowflare-maps-act4.data';
      var REMOTE_PACKAGE_BASE = 'shadowflare-maps-act4.data';
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
          Module['removeRunDependency']('datafile_/home/user/shadowflare/build-web-site/site/shadowflare-maps-act4.data');
      }
      Module['addRunDependency']('datafile_/home/user/shadowflare/build-web-site/site/shadowflare-maps-act4.data');

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
    loadPackage({"files": [{"filename": "/content/Map/4th_A.txt", "start": 0, "end": 382507}, {"filename": "/content/Map/4th_B.txt", "start": 382507, "end": 771526}, {"filename": "/content/Map/4th_C.txt", "start": 771526, "end": 1245793}, {"filename": "/content/Map/4th_D.txt", "start": 1245793, "end": 1598687}, {"filename": "/content/Map/4th_E.txt", "start": 1598687, "end": 2026268}, {"filename": "/content/Map/4th_F.txt", "start": 2026268, "end": 2402922}, {"filename": "/content/Map/4th_G.txt", "start": 2402922, "end": 2836979}, {"filename": "/content/Map/4th_H.txt", "start": 2836979, "end": 3224308}, {"filename": "/content/Map/4th_I.txt", "start": 3224308, "end": 3634238}, {"filename": "/content/Map/4th_J.txt", "start": 3634238, "end": 4002923}, {"filename": "/content/Map/4th_d01.txt", "start": 4002923, "end": 4221482}, {"filename": "/content/Map/4th_d02.txt", "start": 4221482, "end": 4453905}, {"filename": "/content/Map/4th_d03.txt", "start": 4453905, "end": 4673285}, {"filename": "/content/Map/Overview/4th_A.png", "start": 4673285, "end": 5101878}, {"filename": "/content/Map/Overview/4th_B.png", "start": 5101878, "end": 5521305}, {"filename": "/content/Map/Overview/4th_C.png", "start": 5521305, "end": 6059554}, {"filename": "/content/Map/Overview/4th_D.png", "start": 6059554, "end": 6413932}, {"filename": "/content/Map/Overview/4th_E.png", "start": 6413932, "end": 6805841}, {"filename": "/content/Map/Overview/4th_F.png", "start": 6805841, "end": 7187476}, {"filename": "/content/Map/Overview/4th_G.png", "start": 7187476, "end": 7639603}, {"filename": "/content/Map/Overview/4th_H.png", "start": 7639603, "end": 8068983}, {"filename": "/content/Map/Overview/4th_I.png", "start": 8068983, "end": 8498857}, {"filename": "/content/Map/Overview/4th_J.png", "start": 8498857, "end": 8930253}, {"filename": "/content/Map/Overview/4th_d01.png", "start": 8930253, "end": 9118404}, {"filename": "/content/Map/Overview/4th_d02.png", "start": 9118404, "end": 9303223}, {"filename": "/content/Map/Overview/4th_d03.png", "start": 9303223, "end": 9497290}, {"filename": "/content/Map/Overview/c04_00.png", "start": 9497290, "end": 9521054}, {"filename": "/content/Map/Overview/c04_01.png", "start": 9521054, "end": 9565275}, {"filename": "/content/Map/Overview/c04_02.png", "start": 9565275, "end": 9639826}, {"filename": "/content/Map/Overview/c04_03.png", "start": 9639826, "end": 9780318}, {"filename": "/content/Map/Overview/cave3_01.png", "start": 9780318, "end": 9861940}, {"filename": "/content/Map/Overview/cave4_01.png", "start": 9861940, "end": 9963988}, {"filename": "/content/Map/Overview/cave4_02.png", "start": 9963988, "end": 10050984}, {"filename": "/content/Map/Overview/d03_00_00_14080004.png", "start": 10050984, "end": 10063786}, {"filename": "/content/Map/Overview/d03_00_00_14090003.png", "start": 10063786, "end": 10076611}, {"filename": "/content/Map/Overview/d03_17_00_14030000.png", "start": 10076611, "end": 10084412}, {"filename": "/content/Map/Overview/d04_00_00.png", "start": 10084412, "end": 10118001}, {"filename": "/content/Map/Overview/d04_01_00.png", "start": 10118001, "end": 10168482}, {"filename": "/content/Map/Overview/d04_02_00.png", "start": 10168482, "end": 10228920}, {"filename": "/content/Map/Overview/d04_02_01.png", "start": 10228920, "end": 10274059}, {"filename": "/content/Map/Overview/d04_03_00.png", "start": 10274059, "end": 10409226}, {"filename": "/content/Map/Overview/fort4_01.png", "start": 10409226, "end": 10520036}, {"filename": "/content/Map/Overview/fort4_02.png", "start": 10520036, "end": 10632727}, {"filename": "/content/Map/Overview/fort4_03.png", "start": 10632727, "end": 10717258}, {"filename": "/content/Map/Overview/fort4_04.png", "start": 10717258, "end": 10787646}, {"filename": "/content/Map/Overview/fort4_05.png", "start": 10787646, "end": 10858076}, {"filename": "/content/Map/Overview/fort4_06.png", "start": 10858076, "end": 10938892}, {"filename": "/content/Map/Overview/ruin4_01.png", "start": 10938892, "end": 11014506}, {"filename": "/content/Map/Overview/ruin4_02.png", "start": 11014506, "end": 11123133}, {"filename": "/content/Map/Overview/ruin4_03.png", "start": 11123133, "end": 11228603}, {"filename": "/content/Map/Overview/ruin4_04.png", "start": 11228603, "end": 11250048}, {"filename": "/content/Map/Overview/y00_00_14080000.png", "start": 11250048, "end": 11258496}, {"filename": "/content/Map/Overview/y01_01_14080001.png", "start": 11258496, "end": 11271582}, {"filename": "/content/Map/Overview/y01_01_14080002.png", "start": 11271582, "end": 11284750}, {"filename": "/content/Map/Overview/y01_01_14080003.png", "start": 11284750, "end": 11297975}, {"filename": "/content/Map/Overview/y01_01_14090000.png", "start": 11297975, "end": 11311158}, {"filename": "/content/Map/Overview/y01_01_14090001.png", "start": 11311158, "end": 11324221}, {"filename": "/content/Map/Overview/y01_01_14090002.png", "start": 11324221, "end": 11337385}, {"filename": "/content/Map/c04_00.txt", "start": 11337385, "end": 11349946}, {"filename": "/content/Map/c04_01.txt", "start": 11349946, "end": 11378561}, {"filename": "/content/Map/c04_02.txt", "start": 11378561, "end": 11452796}, {"filename": "/content/Map/c04_03.txt", "start": 11452796, "end": 11573115}, {"filename": "/content/Map/cave3_01.txt", "start": 11573115, "end": 11674851}, {"filename": "/content/Map/cave4_01.txt", "start": 11674851, "end": 11805875}, {"filename": "/content/Map/cave4_02.txt", "start": 11805875, "end": 11915678}, {"filename": "/content/Map/d03_00_00_14080004.txt", "start": 11915678, "end": 11926377}, {"filename": "/content/Map/d03_00_00_14090003.txt", "start": 11926377, "end": 11937085}, {"filename": "/content/Map/d03_17_00_14030000.txt", "start": 11937085, "end": 11943369}, {"filename": "/content/Map/d04_00_00.txt", "start": 11943369, "end": 11971810}, {"filename": "/content/Map/d04_01_00.txt", "start": 11971810, "end": 12012772}, {"filename": "/content/Map/d04_02_00.txt", "start": 12012772, "end": 12067374}, {"filename": "/content/Map/d04_02_01.txt", "start": 12067374, "end": 12099163}, {"filename": "/content/Map/d04_03_00.txt", "start": 12099163, "end": 12203084}, {"filename": "/content/Map/fort4_01.txt", "start": 12203084, "end": 12339762}, {"filename": "/content/Map/fort4_02.txt", "start": 12339762, "end": 12483397}, {"filename": "/content/Map/fort4_03.txt", "start": 12483397, "end": 12561720}, {"filename": "/content/Map/fort4_04.txt", "start": 12561720, "end": 12619648}, {"filename": "/content/Map/fort4_05.txt", "start": 12619648, "end": 12675552}, {"filename": "/content/Map/fort4_06.txt", "start": 12675552, "end": 12772740}, {"filename": "/content/Map/ruin4_01.txt", "start": 12772740, "end": 12839371}, {"filename": "/content/Map/ruin4_02.txt", "start": 12839371, "end": 12951960}, {"filename": "/content/Map/ruin4_03.txt", "start": 12951960, "end": 13056106}, {"filename": "/content/Map/ruin4_04.txt", "start": 13056106, "end": 13077239}, {"filename": "/content/Map/y00_00_14080000.txt", "start": 13077239, "end": 13083792}, {"filename": "/content/Map/y01_01_14080001.txt", "start": 13083792, "end": 13094989}, {"filename": "/content/Map/y01_01_14080002.txt", "start": 13094989, "end": 13106181}, {"filename": "/content/Map/y01_01_14080003.txt", "start": 13106181, "end": 13117359}, {"filename": "/content/Map/y01_01_14090000.txt", "start": 13117359, "end": 13128558}, {"filename": "/content/Map/y01_01_14090001.txt", "start": 13128558, "end": 13139753}, {"filename": "/content/Map/y01_01_14090002.txt", "start": 13139753, "end": 13150950}], "remote_package_size": 13150950});

  })();
