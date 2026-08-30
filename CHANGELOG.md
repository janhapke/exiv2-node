## 0.28.8 (2026-08-30) - @janhapke/exiv2 fork

* **Fix:** `getImageTags()` now returns *interpreted* tag values
  (`Exiv2::Metadatum::print()`) instead of *raw* ones
  (`Exiv2::Value::toString()`) for EXIF, IPTC, and XMP data.

  Upstream `@11ways/exiv2node` (and the original `exiv2` package before it)
  called `i->value().toString()` when building the tag map in
  `GetTagsWorker::Execute()` (`exiv2node.cc`). `toString()` does generic,
  type-only self-formatting with no per-tag meaning applied. Exiv2's own
  `Metadatum::print(const ExifData* pMetadata = nullptr)` is documented as
  writing "the interpreted value" and is what actually resolves
  manufacturer-specific numeric codes into human-readable text - e.g. a
  Nikon `LensIDNumber` of `"154"` becomes
  `"Nikon AF-S DX VR Zoom-Nikkor 18-55mm f/3.5-5.6G"` - and decrypts
  fields such as Nikon/Pentax shutter counts. None of that interpretation
  ever ran with the raw path.

  `Metadatum::print()` is a non-virtual base-class method implemented in
  terms of each subclass's `write()`, and `ExifDatum`, `IptcDatum`, and
  `XmpDatum` each override `write()` with their own interpretation logic,
  so the fix was applied to all three loops (EXIF, IPTC, XMP), not just
  EXIF.

  Verified against real Nikon D60 `.NEF` files: `Exif.NikonLd2.LensIDNumber`
  changed from the raw code `"154"` to the resolved lens name, and
  `Exif.Nikon3.ShutterCount` continues to report a plausible integer. See
  `examples/verify-interpreted-fix.js`.

* Package republished as `@janhapke/exiv2` (fork of `@11ways/exiv2`), with
  the version number set to match the Exiv2 library version this build
  was verified against (0.28.8), rather than continuing upstream's own
  version sequence.

## 0.7.3 (2026-01-21)

* Migrate from `nan` to `node-addon-api` for Node.js v24+ compatibility
* Minimum Node.js version is now 18.0.0
* Fix potential memory issues in Preview struct (proper move semantics)
* Add input validation for `setImageTags` and `deleteImageTags`
* Replace `assert()` with proper error handling in async workers

## 0.7.2 (2023-10-18)

* Make backwards compatible with versions lower than v0.28.0

## 0.7.1 (2023-10-18)

* Upgrade `nan` dependency to v2.18.0

## 0.7.0 (2023-10-17)

* Make compatible with Exiv2 v0.28.0

## 0.6.4 (2021-09-10)

* Fix compilation for Node.js v14
* Use Github Actions instead of Travis-ci for the unit tests