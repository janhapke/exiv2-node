## 0.2808.4 (2026-09-13) - @janhapke/exiv2 fork

* **Fix:** serialize every call into Exiv2 across all four `AsyncWorker`s
  behind one global mutex, closing a real crash reported against
  `0.2808.3`: a consumer (photoview's image-decoder process) that fires
  multiple `getImageTags()` calls concurrently hit a hard native
  `v8::HandleScope::CreateHandle()` crash shortly after wiring up
  `setLogHandler()`.

  The crash's own theorized cause (a `Napi::ThreadSafeFunction` ordering
  race) didn't hold up — that design was already replaced with a
  mutex-protected plain-data buffer before `0.2808.3` shipped, and never
  touches N-API from a worker-pool thread. The more likely root cause:
  Exiv2 itself isn't documented as safe for concurrent access —
  `<exiv2/xmp_exiv2.hpp>`'s `XmpParser::initialize()` (lazily triggered
  the first time *any* image carrying XMP data is read) is explicitly
  documented as "not thread-safe and needs to be called in a thread-safe
  manner (e.g., on program startup, before threads are created)". This
  addon never serialized calls across its workers, so two concurrent
  first-ever XMP reads racing on that lazy init (or any other similarly
  unsynchronized internal Exiv2 state) has always been possible. Before
  `0.2808.3`, `Exiv2::LogMsg`'s default handler did a trivial `std::cerr
  <<` per message; the new handler wiring made that path do real work
  (mutex lock, heap allocation) on the calling worker thread, plausibly
  widening the timing window enough to turn a rare pre-existing race into
  an occasionally-observable one.

  Rather than chase the exact internal Exiv2 state involved, every
  worker's `Execute()` now holds one global mutex for its entire
  Exiv2-touching span, so only one call is ever inside Exiv2 at a time,
  addon-wide. Exiv2 was never validated for concurrent access, so trading
  away true multi-threaded parallelism here is the right call.

* **Fix:** a `setLogHandler()` callback that throws no longer swallows the
  triggering call's own result callback. `NAPI_CPP_EXCEPTIONS` (enabled by
  this addon) turns a throwing JS callback into a C++ exception at the
  `.Call()` site; left uncaught, it unwound straight out of the calling
  worker's `OnOK()`, skipping that call's own `getImageTags()`/etc.
  callback entirely. `DrainLogEvents()` now catches that, prints a notice
  to `stderr`, and drops any remaining buffered events for that drain
  instead.

* **Fix:** `DrainLogEvents()` now also runs from every worker's new
  `OnError()` override, not just `OnOK()`. In practice `OnError()` was
  (and remains) unreachable in normal operation — every `Execute()`
  already catches `std::exception`, and `Exiv2::Error` derives from it —
  but this closes the gap for any future/exotic exception path instead of
  silently misattributing that call's buffered log events to whichever
  next call happens to drain the shared buffer.

* **Fix:** `setLogLevel()`, `muteLog()`, and `setLogHandler()` now also
  hold the global Exiv2 mutex above, closing a second instance of the
  same bug class the mutex was introduced for. `Exiv2::LogMsg::level_`
  and `handler_` (`error.hpp`) are plain, non-atomic static members, read
  unconditionally by every one of Exiv2's internal log macros throughout
  its parsing code; these three functions write them via
  `LogMsg::setLevel()`/`setHandler()`. Without holding the mutex, calling
  any of them while a `getImageTags()`/etc. call is already in flight —
  not just once at quiet startup — raced an unsynchronized write on the
  main thread against those unsynchronized reads on a worker-pool thread.
  Found while auditing the logging code for other instances of the crash
  above.

* **Fix:** the module's `AddCleanupHook` now also restores Exiv2's
  default log handler (`Exiv2::LogMsg::setHandler(&Exiv2::LogMsg::
  defaultHandler)`), so a custom handler installed via `setLogHandler()`
  can't leave a dangling pointer into this addon's own code sitting in
  Exiv2's process-wide static state past this environment's teardown.

## 0.2808.3 (2026-09-13) - @janhapke/exiv2 fork

* **Add:** `setLogLevel()`, `muteLog()`, and `setLogHandler()`, exposing
  `Exiv2::LogMsg`'s level/handler controls (`error.hpp`) at the JS
  boundary.

  Exiv2's internal logger writes its own `Warning:`/`Error:` diagnostics
  straight to `stderr` whenever it hits malformed TIFF/IFD structure (e.g.
  a corrupted or truncated file) — entirely bypassing the JS callback.
  `getImageTags()` and friends could resolve with no tags and no error
  while Exiv2 had already logged something like `Directory Image with 572
  entries considered invalid; not read.` with no way for a consumer to
  suppress, redirect, or even detect it. See [issue
  #1](https://github.com/janhapke/exiv2-node/issues/1).

  `setLogLevel('debug'|'info'|'warn'|'error'|'mute')` and its `muteLog()`
  shorthand map directly onto `Exiv2::LogMsg::setLevel()`.
  `setLogHandler(fn | null)` installs a JS function receiving
  `{level, message}` events instead of Exiv2's default `stderr` output
  (`null` restores that default); the message text is passed through
  exactly as Exiv2 composed it. See the README's new "Diagnostics /
  logging" section for the full API and its process-global caveat
  (`LogMsg::Handler` is a plain C function pointer with no per-call
  context slot, so — like `setLogLevel()` — a handler installed via
  `setLogHandler()` is shared by every in-flight call, not scoped to the
  one that triggered it).

  Log events are captured into a plain-data buffer from
  `Exiv2::LogMsg`'s handler (which can run on a libuv worker-pool thread,
  inside any of the four `AsyncWorker`s' `Execute()`), then delivered to
  the installed JS handler synchronously from that worker's `OnOK()` —
  which Node-API guarantees already runs on the main JS thread — rather
  than through a `Napi::ThreadSafeFunction`, which was tried first but
  raced the worker's own completion callback (Node-API's "blocking" call
  mode only blocks when the delivery queue is full, not until the JS
  callback has actually run).

  Covered by a new `corrupt-ifd.jpg` fixture (`books.jpg` with its IFD0
  entry count patched to a bogus `572`, reliably reproducing the bug) and
  a `test/helpers/log-control-subprocess.js` helper — each scenario runs
  in its own subprocess since Exiv2's default handler writes straight to
  `std::cerr` (invisible to a `process.stderr.write` spy) and the new
  functions are process-global state that must not leak between test
  cases.

## 0.2808.2 (2026-08-30) - @janhapke/exiv2 fork

* **Add:** hand-written TypeScript declarations (`exiv2.d.ts`), covering the
  entire exported API surface — all four native bindings from
  `InitAll()` in `exiv2node.cc` (`getImageTags`, `setImageTags`,
  `deleteImageTags`, `getImagePreviews`), plus the one JS-level helper
  layered on top in `exiv2.js` (`getDate`). Declarations were written from
  each function's actual argument validation and callback shape (not
  inferred from the README), including which calls throw synchronously
  on bad arguments vs. report errors through their callback. Wired up via
  a new `"types"` field in `package.json`. Verified with `tsc --noEmit`
  against a scratch file exercising every export (see `npm run
  typecheck`, backed by the new `tsconfig.json` and `typescript`/
  `@types/node` devDependencies).
* **Fix:** repo URLs in `package.json` (`homepage`, `repository.url`,
  `bugs.url`) pointed at `github.com/janhapke/exiv2node`; the fork
  actually lives at `github.com/janhapke/exiv2-node`.
* **Change:** adopted a new versioning scheme, `0.XXYY.Z` (`XXYY` = tracked
  Exiv2 version's minor number + 2-digit zero-padded patch number
  concatenated, `Z` = this fork's own release counter for that Exiv2
  version) — see the README's "Versioning scheme" section. Patch is
  zero-padded rather than bare-concatenated because Exiv2's patch number
  has already reached `8` in the `0.28.x` line; an unpadded scheme would
  invert ordering the moment a minor bump (e.g. `0.29.0`) landed while an
  older minor's patch was already double digits (e.g. `0.28.10`). This is
  release `Z=2` against Exiv2 `0.28.8`.

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