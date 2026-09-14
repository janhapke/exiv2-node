// Spawns several real worker_threads, each require()-ing this addon
// independently and exercising log control concurrently -- this is the
// scenario from issue #6: photoview's DecodeWorkerPool loads this addon
// into several worker_threads, each calling setLogHandler() on its own.
//
// Run as its own process (see test.js's "log control across worker_threads"
// block) because worker_threads is real OS-thread concurrency and, like the
// rest of the log-control tests, needs a clean process to avoid leaking
// state into/out of other test cases.
//
// Usage: node worker-threads-log-isolation.js <fixturePath> <workerCount> <iterations>
//
// Spawns `workerCount` worker_threads:
//   - worker 0 never touches log control at all -- relies on Exiv2's
//     default stderr handler (proves other workers' setLogHandler() calls
//     don't silently swallow a worker that never opted in).
//   - workers 1..N-1 each call setLogHandler() with a handler tagged by
//     their own worker id, and each runs `iterations` getImageTags() calls
//     against the corrupt fixture concurrently with all the others.
//
// Prints one JSON object to stdout: { perWorkerEventCounts: [...] } where
// perWorkerEventCounts[i] is how many log events worker i's own handler
// received (0 for worker 0, which has none installed). If the process
// instead dies with a native fatal error (the issue #6 crash), spawnSync's
// caller sees a non-zero/signal exit and empty stdout.

const { Worker, isMainThread, parentPort, workerData } = require('worker_threads');
const path = require('path');

if (isMainThread) {
  const fixture = process.argv[2];
  const workerCount = Number(process.argv[3] || 6);
  const iterations = Number(process.argv[4] || 100);

  const perWorkerEventCounts = new Array(workerCount).fill(null);
  let finished = 0;

  for (let id = 0; id < workerCount; id++) {
    const w = new Worker(__filename, { workerData: { id, fixture, iterations, installHandler: id !== 0 } });
    w.on('message', (msg) => {
      perWorkerEventCounts[msg.id] = msg.eventCount;
      finished++;
      if (finished === workerCount) {
        process.stdout.write(JSON.stringify({ perWorkerEventCounts }));
      }
    });
    w.on('error', (err) => {
      process.stderr.write('WORKER ERROR: ' + (err && err.stack || err) + '\n');
      process.exitCode = 1;
    });
  }
} else {
  const exiv = require('../../exiv2');
  const { id, fixture, iterations, installHandler } = workerData;

  let eventCount = 0;
  if (installHandler) {
    exiv.setLogHandler(function(e) {
      // Tag proves this worker's own handler -- not some other worker's --
      // is the one being invoked, and that Napi calls on the event object
      // work correctly (would be the first thing to fail on a cross-isolate
      // handle if isolation regressed).
      if (typeof e.level === 'string' && typeof e.message === 'string') {
        eventCount++;
      }
    });
  }

  let remaining = iterations;
  function loop() {
    if (remaining-- <= 0) {
      parentPort.postMessage({ id, eventCount });
      return;
    }
    exiv.getImageTags(fixture, function() {
      loop();
    });
  }
  loop();
}
