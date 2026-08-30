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
