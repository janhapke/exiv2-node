// Runs getImageTags() against a fixture under one of a few log-control
// configurations, in its own process. This has to be a subprocess (rather
// than an in-process mocha test) for two reasons:
//
// 1. Exiv2::LogMsg's default handler writes straight to std::cerr at the
//    C++ level, bypassing Node's stream wrapper entirely, so a JS-level
//    `process.stderr.write` spy never sees it -- only a real OS-level pipe
//    (as spawnSync's stdio capture provides) does.
// 2. setLogLevel()/setLogHandler() are process-global (see issue #1 and
//    exiv2node.cc's log-control section), so running each configuration in
//    its own process avoids one test's state leaking into another's.
//
// Usage: node log-control-subprocess.js <mode> <fixturePath>
//   default            - default handler/level, prints {err, tagCount} to stdout
//   mute               - muteLog() first, prints {err, tagCount} to stdout
//   mute-via-level     - setLogLevel('mute') first, prints {err, tagCount} to stdout
//   handler            - setLogHandler(cb) first, prints the captured events to stdout
//   handler-then-clear - setLogHandler(cb) then setLogHandler(null), prints {err, tagCount}

var exiv = require('../../exiv2');

var mode = process.argv[2];
var fixture = process.argv[3];

function reportTags(err, tags) {
  process.stdout.write(JSON.stringify({ err: err, tagCount: tags ? Object.keys(tags).length : 0 }));
}

if (mode === 'mute') {
  exiv.muteLog();
  exiv.getImageTags(fixture, reportTags);
} else if (mode === 'mute-via-level') {
  exiv.setLogLevel('mute');
  exiv.getImageTags(fixture, reportTags);
} else if (mode === 'handler') {
  var events = [];
  exiv.setLogHandler(function(e) { events.push(e); });
  exiv.getImageTags(fixture, function() {
    process.stdout.write(JSON.stringify(events));
  });
} else if (mode === 'handler-then-clear') {
  exiv.setLogHandler(function() {});
  exiv.setLogHandler(null);
  exiv.getImageTags(fixture, reportTags);
} else {
  exiv.getImageTags(fixture, reportTags);
}
