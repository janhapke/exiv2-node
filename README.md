<h1 align="center">
  <b>@janhapke/exiv2</b>
</h1>
<div align="center">
  <sub>
    A fork of <a href="https://github.com/11ways/exiv2node">@11ways/exiv2node</a> that fixes interpreted (rather than raw) tag values.
  </sub>
</div>
<div align="center">
  <!-- CI - Github Actions -->
  <a href="https://github.com/11ways/exiv2node/actions/workflows/unit_test.yaml">
    <img src="https://github.com/11ways/exiv2node/actions/workflows/unit_test.yaml/badge.svg" alt="Node.js CI (Linux, MacOS, Windows)" />
  </a>

  <!-- Coverage - Codecov -->
  <a href="https://codecov.io/gh/11ways/exiv2node">
    <img src="https://img.shields.io/codecov/c/github/11ways/exiv2node/master.svg" alt="Codecov Coverage report" />
  </a>

  <!-- DM - Snyk -->
  <a href="https://snyk.io/test/github/11ways/exiv2node?targetFile=package.json">
    <img src="https://snyk.io/test/github/11ways/exiv2node/badge.svg?targetFile=package.json" alt="Known Vulnerabilities" />
  </a>
</div>

<div align="center">
  <!-- Version - npm -->
  <a href="https://www.npmjs.com/package/@11ways/exiv2">
    <img src="https://img.shields.io/npm/v/@11ways/exiv2.svg" alt="Latest version on npm" />
  </a>

  <!-- License - MIT -->
  <a href="https://github.com/11ways/exiv2node#license">
    <img src="https://img.shields.io/github/license/11ways/exiv2node.svg" alt="Project license" />
  </a>
</div>
<br>
<div align="center">
  👷🏼 Eleven Ways' exiv2node library
</div>
<div align="center">
  <sub>
    Coded with ❤️ by <a href="#authors">Eleven Ways</a>.
  </sub>
</div>

## Why this fork exists

`@janhapke/exiv2` is a fork of [`@11ways/exiv2`](https://github.com/11ways/exiv2node), maintained by [Jan Hapke](https://github.com/janhapke). It exists to:

- fix `getImageTags()` returning Exiv2's *raw* tag values instead of the *interpreted* ones — see [CHANGELOG.md](CHANGELOG.md) for the underlying bug and fix (e.g. a Nikon lens ID like `"154"` now correctly resolves to `"Nikon AF-S DX VR Zoom-Nikkor 18-55mm f/3.5-5.6G"`)
- ship hand-written TypeScript declarations (`exiv2.d.ts`), which upstream has never had

## Versioning scheme

This fork tracks the [Exiv2](https://exiv2.org) C++ library version it's built and tested against, since correctness here depends directly on which native Exiv2 release resolves tag interpretation. Versions are `0.XXYY.Z`:

- **`XXYY`** — the tracked Exiv2 version's minor number followed by its patch number, zero-padded to 2 digits, concatenated (Exiv2 `0.28.8` → minor `28` + patch `08` → `2808`).
- **`Z`** — this fork's own release counter for that Exiv2 version, starting at `0` and bumped for every fork-only change (a fix, a feature like type declarations, a metadata correction) that doesn't require bumping the tracked Exiv2 version.

For example, `0.2808.2` is this fork's 3rd release (`Z=2`) built against Exiv2 `0.28.8`.

The patch number is zero-padded (not left as a bare concatenation) because Exiv2's minor version has already grown from 1 digit to 2 (`0.9` → `0.10`, and never dropped back), and its patch number has already reached `8` in the current `0.28.x` line — one release away from testing a double-digit patch. An unpadded `XXY` scheme breaks the moment a minor bump happens while the old minor's patch was already double digits: e.g. Exiv2 `0.28.10` would concatenate to `2810`, but `0.29.0` would concatenate to `290` — and since semver compares these as plain numbers, `2810 > 290` would make the fork version for the *older* `0.28.10` sort as newer than the fork version for `0.29.0`. Zero-padding the patch to a fixed 2-digit width keeps every comparison monotonic no matter how the digit counts change (assuming Exiv2's patch number stays under 100, comfortably true for the foreseeable future).

> **Note:** this fork's very first release (the interpreted-tags fix) was published as plain `0.28.8`, before this scheme was adopted. Every release from `0.2808.2` onward follows the scheme above.

# Exiv2

Exiv2 is a native C++ extension for [node.js](https://nodejs.org) that provides
support for reading and writing image metadata via the [Exiv2 library](http://www.exiv2.org).

It was created by Damian Beresford

## Dependencies

To build this addon you'll need the Exiv2 library and headers so if you're using
a package manager you might need to install an additional "-dev" packages.

### Debian / Ubuntu

    apt-get install pkg-config exiv2 libexiv2-dev

### macOS

You'll also need to install pkg-config to help locate the library and headers.

[MacPorts](http://macports.org/):

    port install pkgconfig exiv2

[Homebrew](http://github.com/mxcl/homebrew/):

    brew install pkg-config exiv2

### FreeBSD

    pkg install pkgconf exiv2

### Arch Linux

    pacman -S exiv2 pkgconf

### Windows
Install pkg-config using [Chocolatey](https://chocolatey.org/):

    choco install pkgconfiglite
    
Download latest `msvc64` exiv2 build from the [Exiv2 download page](http://www.exiv2.org/download.html) and extract to a folder of your choice.

Add a system variable named `PKG_CONFIG_PATH` and set it's value to `EXIV2ROOTDIR\lib\pkgconfig` replacing `EXIV2ROOTDIR` with the path where you extracted exiv2 from the step before (e.g. `D:\src\exiv2msvs\lib\pkgconfig`).

You'll also need [windows-build-tools](https://www.npmjs.com/package/windows-build-tools) to compile this package.

For Electron apps, you'll want to copy `exiv2.dll` to the root directory of your Electron Windows build. You can automated this using the [extraFiles option](https://www.electron.build/configuration/contents#extrafiles). 

### Other systems

See the [Exiv2 download page](http://www.exiv2.org/download.html) for more
information.

## Requirements

- Node.js 18 or later
- Exiv2 library and development headers (see Dependencies above)
- pkg-config

## Installation Instructions

Once the dependencies are in place, you can build and install the module using
npm:

    npm install @janhapke/exiv2

You can verify that everything is installed and operating correctly by running
the tests:

    npm test

## Interpreted vs. raw tag values

Unlike the upstream `@11ways/exiv2` package, `getImageTags()` here returns
**interpreted** tag values (via Exiv2's `Metadatum::print()`) instead of
**raw** ones (via `Value::toString()`). This matters for tags whose stored
value needs manufacturer-specific decoding to be meaningful, e.g.:

    // Exif.NikonLd2.LensIDNumber
    // before: "154"
    // after:  "Nikon AF-S DX VR Zoom-Nikkor 18-55mm f/3.5-5.6G"

See [CHANGELOG.md](CHANGELOG.md) for details.

## Diagnostics / logging

Exiv2's internal logger (`Exiv2::LogMsg`) writes its own `Warning:`/`Error:`
diagnostics straight to `stderr` whenever it hits malformed image structure
(e.g. a corrupted or truncated file), independently of whether the call
you made succeeds. For example, `getImageTags()` can resolve normally with
no tags and no error while Exiv2 has already printed something like:

    Error: Directory Image with 572 entries considered invalid; not read.

Two functions let you control this:

    var ex = require('@janhapke/exiv2');

    // Suppress Exiv2's diagnostics entirely.
    ex.muteLog();
    // ...equivalent to:
    ex.setLogLevel('mute');

    // Or route them into your own code instead of stderr.
    ex.setLogHandler(function(event) {
      console.log(event.level, event.message);
    });

    // Restore the default stderr output.
    ex.setLogHandler(null);

`setLogLevel()` accepts `'debug'`, `'info'`, `'warn'`, `'error'`, or
`'mute'` (Exiv2's default level is `'warn'`); only messages at or above
that severity reach the handler.

**Caveat:** both of these control Exiv2's own process-global state, not
anything scoped to a single call — they affect every `getImageTags()`/
`setImageTags()`/`deleteImageTags()`/`getImagePreviews()` call in the
process. A handler installed via `setLogHandler()` receives log events
from whichever call happens to be running at the time; if multiple calls
are in flight concurrently, there is no way to attribute a given message
back to a specific one.

## Sample Usage

### Read tags:

    var ex = require('@janhapke/exiv2');

    ex.getImageTags('./photo.jpg', function(err, tags) {
      console.log("DateTime: " + tags["Exif.Image.DateTime"]);
      console.log("DateTimeOriginal: " + tags["Exif.Photo.DateTimeOriginal"]);
    });

    var fs = require('fs');

    ex.getImageTags(fs.readFileSync('./photo.jpg'), function(err, tags) {
      console.log("DateTime: " + tags["Exif.Image.DateTime"]);
      console.log("DateTimeOriginal: " + tags["Exif.Photo.DateTimeOriginal"]);
    });

### Load preview images:

    var ex = require('@janhapke/exiv2')
      , fs = require('fs');

    ex.getImagePreviews('./photo.jpg', function(err, previews) {
      // Display information about the previews.
      console.log(previews);

      // Or you can save them--though you'll probably want to check the MIME
      // type before picking an extension.
      fs.writeFile('preview.jpg', previews[0].data);
    });

### Write tags:

    var ex = require('@janhapke/exiv2')

    var newTags = {
      "Exif.Photo.UserComment" : "Some Comment..",
      "Exif.Canon.OwnerName" : "My Camera"
    };
    ex.setImageTags('./photo.jpg', newTags, function(err){
      if (err) {
        console.error(err);
      } else {
        console.log("setImageTags complete..");
      }
    });

### Delete tags:

    var ex = require('@janhapke/exiv2')

    var tagsToDelete = ["Exif.Photo.UserComment", "Exif.Canon.OwnerName"];
    ex.deleteImageTags('./photo.jpg', tagsToDelete, function(err){
      if (err) {
        console.error(err);
      } else {
        console.log("deleteImageTags complete..");
      }
    });

Take a look at the `examples/` and `test/` directories for more.

## Authors
- **Damian Beresford** - Original creator
- **Jelle De Loecker** -  *Follow* me on *Github* ([:octocat:@skerit](https://github.com/skerit)) and on *Mastodon* ([🐦@skerit@elevenways.be](https://mastodon.elevenways.be/@skerit))
- **Jan Hapke** - Maintainer of this fork ([@janhapke](https://github.com/janhapke))

See also the list of [contributors](https://github.com/11ways/exiv2node/contributors) who participated in the upstream project, and [AUTHORS](AUTHORS) for this fork.

The original @11ways/exiv2node is developed at [Eleven Ways](https://www.elevenways.be/), a team of [IAAP Certified Accessibility Specialists](https://www.accessibilityassociation.org/).

## License
This project is licensed under the MIT License - see the [LICENSE](https://github.com/11ways/exiv2node/LICENSE) file for details.
