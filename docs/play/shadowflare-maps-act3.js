
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
      var PACKAGE_NAME = '/home/user/shadowflare/build-web-site/site/shadowflare-maps-act3.data';
      var REMOTE_PACKAGE_BASE = 'shadowflare-maps-act3.data';
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
          Module['removeRunDependency']('datafile_/home/user/shadowflare/build-web-site/site/shadowflare-maps-act3.data');
      }
      Module['addRunDependency']('datafile_/home/user/shadowflare/build-web-site/site/shadowflare-maps-act3.data');

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
    loadPackage({"files": [{"filename": "/content/Map/3rdLast00.txt", "start": 0, "end": 75290}, {"filename": "/content/Map/3rdLast01.txt", "start": 75290, "end": 121212}, {"filename": "/content/Map/3rdLast02.txt", "start": 121212, "end": 174384}, {"filename": "/content/Map/3rdLast03.txt", "start": 174384, "end": 218365}, {"filename": "/content/Map/3rdLast04.txt", "start": 218365, "end": 233326}, {"filename": "/content/Map/3rd_1-1.txt", "start": 233326, "end": 805042}, {"filename": "/content/Map/3rd_2-1.txt", "start": 805042, "end": 1123579}, {"filename": "/content/Map/3rd_2-2.txt", "start": 1123579, "end": 1473039}, {"filename": "/content/Map/3rd_2-3.txt", "start": 1473039, "end": 1710033}, {"filename": "/content/Map/3rd_3-1.txt", "start": 1710033, "end": 2210137}, {"filename": "/content/Map/3rd_3-2.txt", "start": 2210137, "end": 2615267}, {"filename": "/content/Map/3rd_3-3.txt", "start": 2615267, "end": 3072867}, {"filename": "/content/Map/3rd_3-4.txt", "start": 3072867, "end": 3445621}, {"filename": "/content/Map/3rd_3-5.txt", "start": 3445621, "end": 3727348}, {"filename": "/content/Map/3rd_3-6.txt", "start": 3727348, "end": 4117278}, {"filename": "/content/Map/3rd_3-7.txt", "start": 4117278, "end": 4433083}, {"filename": "/content/Map/3rd_4-2.txt", "start": 4433083, "end": 4927655}, {"filename": "/content/Map/3rd_4-3.txt", "start": 4927655, "end": 5448345}, {"filename": "/content/Map/3rd_4-4.txt", "start": 5448345, "end": 5765841}, {"filename": "/content/Map/3rd_4-5.txt", "start": 5765841, "end": 6341974}, {"filename": "/content/Map/3rd_4-6.txt", "start": 6341974, "end": 6943575}, {"filename": "/content/Map/3rd_4-7.txt", "start": 6943575, "end": 7370216}, {"filename": "/content/Map/3rd_5-3.txt", "start": 7370216, "end": 7660304}, {"filename": "/content/Map/3rd_5-5.txt", "start": 7660304, "end": 8039994}, {"filename": "/content/Map/3rd_5-6.txt", "start": 8039994, "end": 8407060}, {"filename": "/content/Map/3rd_5-7.txt", "start": 8407060, "end": 8916480}, {"filename": "/content/Map/Overview/3rdLast00.png", "start": 8916480, "end": 9004267}, {"filename": "/content/Map/Overview/3rdLast01.png", "start": 9004267, "end": 9058984}, {"filename": "/content/Map/Overview/3rdLast02.png", "start": 9058984, "end": 9122759}, {"filename": "/content/Map/Overview/3rdLast03.png", "start": 9122759, "end": 9169606}, {"filename": "/content/Map/Overview/3rdLast04.png", "start": 9169606, "end": 9192463}, {"filename": "/content/Map/Overview/3rd_1-1.png", "start": 9192463, "end": 9672509}, {"filename": "/content/Map/Overview/3rd_2-1.png", "start": 9672509, "end": 9974886}, {"filename": "/content/Map/Overview/3rd_2-2.png", "start": 9974886, "end": 10295264}, {"filename": "/content/Map/Overview/3rd_2-3.png", "start": 10295264, "end": 10575352}, {"filename": "/content/Map/Overview/3rd_3-1.png", "start": 10575352, "end": 11066153}, {"filename": "/content/Map/Overview/3rd_3-2.png", "start": 11066153, "end": 11560518}, {"filename": "/content/Map/Overview/3rd_3-3.png", "start": 11560518, "end": 12011535}, {"filename": "/content/Map/Overview/3rd_3-4.png", "start": 12011535, "end": 12410844}, {"filename": "/content/Map/Overview/3rd_3-5.png", "start": 12410844, "end": 12732157}, {"filename": "/content/Map/Overview/3rd_3-6.png", "start": 12732157, "end": 13180687}, {"filename": "/content/Map/Overview/3rd_3-7.png", "start": 13180687, "end": 13554692}, {"filename": "/content/Map/Overview/3rd_4-2.png", "start": 13554692, "end": 14063382}, {"filename": "/content/Map/Overview/3rd_4-3.png", "start": 14063382, "end": 14598150}, {"filename": "/content/Map/Overview/3rd_4-4.png", "start": 14598150, "end": 14995031}, {"filename": "/content/Map/Overview/3rd_4-5.png", "start": 14995031, "end": 15427138}, {"filename": "/content/Map/Overview/3rd_4-6.png", "start": 15427138, "end": 15965804}, {"filename": "/content/Map/Overview/3rd_4-7.png", "start": 15965804, "end": 16389053}, {"filename": "/content/Map/Overview/3rd_5-3.png", "start": 16389053, "end": 16733511}, {"filename": "/content/Map/Overview/3rd_5-5.png", "start": 16733511, "end": 17183238}, {"filename": "/content/Map/Overview/3rd_5-6.png", "start": 17183238, "end": 17588139}, {"filename": "/content/Map/Overview/3rd_5-7.png", "start": 17588139, "end": 18071104}, {"filename": "/content/Map/Overview/c03_00.png", "start": 18071104, "end": 18114039}, {"filename": "/content/Map/Overview/c03_01.png", "start": 18114039, "end": 18178475}, {"filename": "/content/Map/Overview/c03_02.png", "start": 18178475, "end": 18220743}, {"filename": "/content/Map/Overview/c03_03.png", "start": 18220743, "end": 18299708}, {"filename": "/content/Map/Overview/c03_04.png", "start": 18299708, "end": 18354906}, {"filename": "/content/Map/Overview/c03_05.png", "start": 18354906, "end": 18366203}, {"filename": "/content/Map/Overview/cave_08_13070000.png", "start": 18366203, "end": 18409317}, {"filename": "/content/Map/Overview/cave_10_13060000.png", "start": 18409317, "end": 18436278}, {"filename": "/content/Map/Overview/d03_00_00.png", "start": 18436278, "end": 18449211}, {"filename": "/content/Map/Overview/d03_01_00.png", "start": 18449211, "end": 18507548}, {"filename": "/content/Map/Overview/d03_02_00.png", "start": 18507548, "end": 18560872}, {"filename": "/content/Map/Overview/d03_03_00.png", "start": 18560872, "end": 18600119}, {"filename": "/content/Map/Overview/d03_04_00.png", "start": 18600119, "end": 18609289}, {"filename": "/content/Map/Overview/d03_04_01.png", "start": 18609289, "end": 18622211}, {"filename": "/content/Map/Overview/d03_04_02.png", "start": 18622211, "end": 18634733}, {"filename": "/content/Map/Overview/d03_04_02_13110002.png", "start": 18634733, "end": 18647434}, {"filename": "/content/Map/Overview/d03_04_02_13110003.png", "start": 18647434, "end": 18659986}, {"filename": "/content/Map/Overview/d03_05_00.png", "start": 18659986, "end": 18690322}, {"filename": "/content/Map/Overview/d03_06_00.png", "start": 18690322, "end": 18729588}, {"filename": "/content/Map/Overview/d03_07_00.png", "start": 18729588, "end": 18780611}, {"filename": "/content/Map/Overview/d03_08_00.png", "start": 18780611, "end": 18862593}, {"filename": "/content/Map/Overview/d03_09_00.png", "start": 18862593, "end": 18892871}, {"filename": "/content/Map/Overview/d03_10_00.png", "start": 18892871, "end": 18920263}, {"filename": "/content/Map/Overview/d03_13_00.png", "start": 18920263, "end": 18947023}, {"filename": "/content/Map/Overview/d03_13_00_13220000.png", "start": 18947023, "end": 18973808}, {"filename": "/content/Map/Overview/d03_14_00.png", "start": 18973808, "end": 19003055}, {"filename": "/content/Map/Overview/d03_14_00_13180000.png", "start": 19003055, "end": 19032319}, {"filename": "/content/Map/Overview/d03_14_00_13200000.png", "start": 19032319, "end": 19061843}, {"filename": "/content/Map/Overview/d03_15_00.png", "start": 19061843, "end": 19092845}, {"filename": "/content/Map/Overview/d03_15_00_13190000.png", "start": 19092845, "end": 19123966}, {"filename": "/content/Map/Overview/d03_15_00_13230000.png", "start": 19123966, "end": 19155005}, {"filename": "/content/Map/Overview/d03_16_00.png", "start": 19155005, "end": 19224067}, {"filename": "/content/Map/Overview/d03_17_00.png", "start": 19224067, "end": 19231872}, {"filename": "/content/Map/Overview/d03_18_00.png", "start": 19231872, "end": 19276162}, {"filename": "/content/Map/Overview/y00_00.png", "start": 19276162, "end": 19284596}, {"filename": "/content/Map/Overview/y01_01.png", "start": 19284596, "end": 19297761}, {"filename": "/content/Map/Overview/y01_01_13020002.png", "start": 19297761, "end": 19310880}, {"filename": "/content/Map/Overview/y01_01_13020003.png", "start": 19310880, "end": 19323961}, {"filename": "/content/Map/c03_00.txt", "start": 19323961, "end": 19351715}, {"filename": "/content/Map/c03_01.txt", "start": 19351715, "end": 19395953}, {"filename": "/content/Map/c03_02.txt", "start": 19395953, "end": 19423184}, {"filename": "/content/Map/c03_03.txt", "start": 19423184, "end": 19476204}, {"filename": "/content/Map/c03_04.txt", "start": 19476204, "end": 19520741}, {"filename": "/content/Map/c03_05.txt", "start": 19520741, "end": 19531558}, {"filename": "/content/Map/cave_08_13070000.txt", "start": 19531558, "end": 19579181}, {"filename": "/content/Map/cave_10_13060000.txt", "start": 19579181, "end": 19608314}, {"filename": "/content/Map/d03_00_00.txt", "start": 19608314, "end": 19619020}, {"filename": "/content/Map/d03_01_00.txt", "start": 19619020, "end": 19705996}, {"filename": "/content/Map/d03_02_00.txt", "start": 19705996, "end": 19757148}, {"filename": "/content/Map/d03_03_00.txt", "start": 19757148, "end": 19792841}, {"filename": "/content/Map/d03_04_00.txt", "start": 19792841, "end": 19799869}, {"filename": "/content/Map/d03_04_01.txt", "start": 19799869, "end": 19809642}, {"filename": "/content/Map/d03_04_02.txt", "start": 19809642, "end": 19819820}, {"filename": "/content/Map/d03_04_02_13110002.txt", "start": 19819820, "end": 19829991}, {"filename": "/content/Map/d03_04_02_13110003.txt", "start": 19829991, "end": 19840163}, {"filename": "/content/Map/d03_05_00.txt", "start": 19840163, "end": 19866816}, {"filename": "/content/Map/d03_06_00.txt", "start": 19866816, "end": 19911321}, {"filename": "/content/Map/d03_07_00.txt", "start": 19911321, "end": 19958315}, {"filename": "/content/Map/d03_08_00.txt", "start": 19958315, "end": 20060302}, {"filename": "/content/Map/d03_09_00.txt", "start": 20060302, "end": 20086850}, {"filename": "/content/Map/d03_10_00.txt", "start": 20086850, "end": 20117780}, {"filename": "/content/Map/d03_13_00.txt", "start": 20117780, "end": 20144332}, {"filename": "/content/Map/d03_13_00_13220000.txt", "start": 20144332, "end": 20170890}, {"filename": "/content/Map/d03_14_00.txt", "start": 20170890, "end": 20197042}, {"filename": "/content/Map/d03_14_00_13180000.txt", "start": 20197042, "end": 20223205}, {"filename": "/content/Map/d03_14_00_13200000.txt", "start": 20223205, "end": 20249358}, {"filename": "/content/Map/d03_15_00.txt", "start": 20249358, "end": 20275332}, {"filename": "/content/Map/d03_15_00_13190000.txt", "start": 20275332, "end": 20301312}, {"filename": "/content/Map/d03_15_00_13230000.txt", "start": 20301312, "end": 20327281}, {"filename": "/content/Map/d03_16_00.txt", "start": 20327281, "end": 20379312}, {"filename": "/content/Map/d03_17_00.txt", "start": 20379312, "end": 20385593}, {"filename": "/content/Map/d03_18_00.txt", "start": 20385593, "end": 20433183}, {"filename": "/content/Map/y00_00.txt", "start": 20433183, "end": 20439740}, {"filename": "/content/Map/y01_01.txt", "start": 20439740, "end": 20450937}, {"filename": "/content/Map/y01_01_13020002.txt", "start": 20450937, "end": 20462135}, {"filename": "/content/Map/y01_01_13020003.txt", "start": 20462135, "end": 20473332}], "remote_package_size": 20473332});

  })();
