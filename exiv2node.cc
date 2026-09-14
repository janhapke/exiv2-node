#include <napi.h>
#ifdef _MSC_VER
# include <direct.h>
# include <process.h>
#else
# include <unistd.h>
# include <stdint.h>
#endif
#include <string>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <cmath>
#include <exception>
#include <exiv2/image.hpp>
#include <exiv2/exif.hpp>
#include <exiv2/preview.hpp>
#include <exiv2/error.hpp>
#include <exiv2/xmp_exiv2.hpp>

#if EXIV2_MAJOR_VERSION > 0 || (EXIV2_MAJOR_VERSION == 0 && EXIV2_MINOR_VERSION >= 28)
  #define USE_EXIV2_UNIQUE_PTR 1
  #pragma message ("Using Exiv2::Image::UniquePtr")
#else
  #define USE_EXIV2_UNIQUE_PTR 0
  #pragma message ("Using Exiv2::Image::AutoPtr")
#endif

// Create a map of strings for passing them back and forth between the main and
// worker threads.
typedef std::map<std::string, std::string> tag_map_t;

// Guards every call into Exiv2 across all four AsyncWorkers below. Each
// worker's Execute() runs on a libuv worker-pool thread, so without this,
// two workers' Execute() calls can run genuinely concurrently.
//
// This is a std::shared_mutex, not a plain mutex, because Exiv2's own
// documented thread-safety model (https://dev.exiv2.org/projects/exiv2/
// wiki/Thread_safety, cross-checked against Exiv2 0.28.8's actual source)
// splits cleanly along read/write lines once two specific gaps are closed
// by us:
//
//   - Exif and IPTC parsing is reentrant, and the Adobe XMP toolkit's own
//     encode()/decode() are documented thread-safe (internal mutexes),
//     PROVIDED XmpParser::initialize() has already run. initialize()
//     itself is documented as "not thread-safe... call it in a
//     thread-safe manner (e.g. on program startup, before threads are
//     created)" -- InitAll() below does exactly that, once, before any
//     Execute() can possibly run, closing that gap for good.
//   - XmpProperties::registerNs() (called internally by decode(), i.e.
//     by every getImageTags()/getImagePreviews() read that hits XMP) IS
//     internally mutex-protected against memory corruption in 0.28.8 --
//     confirmed by reading src/properties.cpp, not just the docs. Its
//     residual risk under concurrent reads is *logical*, not
//     memory-unsafe: two threads registering conflicting namespace
//     prefixes can still stomp on each other's entry in the one shared
//     global registry, which can surface as a wrong tag value or a
//     caught Exiv2::Error (turned into a normal `err` by this addon's
//     existing try/catch) -- not a crash. Documented, accepted residual
//     risk; see the README.
//   - XmpParser::encode() (the path setImageTags()/deleteImageTags() take
//     via writeMetadata(), whenever the file already carries XMP data)
//     is a different story: it directly iterates
//     XmpProperties::nsRegistry_ with NO lock at all, while registerNs()
//     mutates that same map under its own mutex elsewhere -- genuine,
//     memory-unsafe iterator invalidation if it races another XMP-
//     touching call. Not something we can narrow a lock around from
//     outside Exiv2 (it's buried inside encode()'s internals), so writes
//     stay fully exclusive below.
//
// So: GetTagsWorker and GetPreviewsWorker (read-only -- readMetadata()
// only, never writeMetadata()) take a shared lock and run concurrently
// with each other again. SetTagsWorker and DeleteTagsWorker
// (writeMetadata()) keep taking an exclusive lock, same as
// SetLogLevel()/MuteLog()/SetLogHandler() (Exiv2::LogMsg::level_/
// handler_, error.hpp, are plain non-atomic statics with no internal
// protection at all -- entirely on us, unrelated to XMP).
//
// This isn't a claim that every other corner of Exiv2 (TIFF/RAW/ICC/
// MakerNote parsing, etc.) has been audited for similar issues -- only
// properties.cpp and xmp.cpp were. It's a deliberate, evidence-based
// tradeoff given how much of Exiv2's own documented exceptions this
// closes, not a guarantee.
std::shared_mutex gExiv2Mutex;

// - - - Log control (Exiv2::LogMsg) - - -
//
// Exiv2::LogMsg writes its own internal diagnostics (malformed TIFF/IFD
// structure, etc.) straight to stderr by default, entirely bypassing the JS
// boundary (see issue #1). LogMsg::setLevel()/setHandler() are the two
// knobs Exiv2 exposes to control this; LogMsg::level_ is genuinely
// process-global static state inside libexiv2 (see gExiv2Mutex's comment),
// so setLogLevel()/muteLog() are unavoidably process-wide -- a known,
// documented limitation (see README).
//
// setLogHandler(), however, is NOT process-wide as of this fix -- see
// issue #6. This addon is a Node-API addon (NODE_API_MODULE), and per
// Node's own docs (https://nodejs.org/api/n-api.html#environment-life-cycle-apis,
// https://nodejs.org/api/addons.html#worker-support) that means its Init
// function is called once per Node.js *environment* -- once for the main
// thread, and once more for every worker_thread that require()s it, all in
// the same process, all sharing the same loaded .node file and therefore
// the same C++ static/global variables. A previous version of this file
// stored the installed JS handler (a Napi::FunctionReference, which is
// inherently tied to the V8 isolate of whichever environment created it)
// in a single process-global gLogHandlerRef. Two environments calling
// setLogHandler() raced to overwrite that one global, and whichever
// environment's AsyncWorker drained a log event second would invoke a
// Napi::FunctionReference that belonged to a *different* isolate --
// exactly the "v8::HandleScope::CreateHandle() Cannot create a handle
// without a HandleScope" fatal crash reported in issue #6, reproduced
// there by photoview's worker_threads-based DecodeWorkerPool (confirmed:
// every worker thread in that pool requires this addon and calls
// setLogHandler() independently).
//
// The fix: the installed handler, its "installed" flag, and its pending
// event buffer now live in InstanceData, fetched via
// env.GetInstanceData<InstanceData>() -- one instance per environment,
// automatically created in InitAll() (which Node-API already calls once
// per environment) and automatically destroyed when that environment
// tears down. A Napi::FunctionReference stored there is only ever read
// back and Call()ed on the same environment that created it, so it never
// crosses isolates.
//
// LogMsg::Handler is still a plain C function pointer (`void(*)(int,
// const char*)`), not a std::function, so there's still no per-call
// context slot Exiv2 itself gives us to route a warning to the right
// environment. LogTrampoline() below is installed as Exiv2's one
// process-wide handler (exactly once, when the first environment loads --
// see InitAll()) and stays installed until the last environment tears
// down. It figures out *which* environment's InstanceData a given call
// belongs to via tlsCurrentInstance, a thread_local pointer each
// AsyncWorker's Execute() sets for its own duration (see
// CurrentInstanceGuard below) -- safe because a single OS thread only
// ever runs one Execute() call at a time (libuv's threadpool contract:
// a pool thread completes one work item before starting the next), even
// though the *same* pool thread may run Execute() calls belonging to
// different environments at different times. LogTrampoline() may run on
// that libuv worker-pool thread, so -- same as before -- it must never
// touch Napi::* directly; it only appends plain data to the owning
// InstanceData's buffer, mutex-protected, or (if no environment's
// tlsCurrentInstance is set, or that environment never installed a
// handler) falls back to Exiv2::LogMsg::defaultHandler() so diagnostics
// are never silently dropped for an environment that didn't ask for
// custom handling.
//
// A Napi::ThreadSafeFunction was tried first to deliver events directly
// from LogTrampoline, but Node-API's "blocking" call mode only blocks when
// the delivery queue is full, not until the JS callback has actually run --
// so it raced the owning AsyncWorker's own OnOK()/OnError() dispatch (a
// separate libuv async handle) with no ordering guarantee, observed as the
// installed handler intermittently not having fired yet by the time the
// getImageTags()/etc. callback ran. DrainLogEvents() below sidesteps that
// entirely: it runs synchronously inside each worker's OnOK(), which
// Node-API guarantees already runs on the main JS thread, so calling the
// plain (non-threadsafe) Napi::FunctionReference there needs no extra
// synchronization -- only the plain-data buffer itself does.

struct LogEvent {
  std::string level;
  std::string message;
};

// One per Node.js environment (main thread, or a worker_thread that
// require()s this addon) -- see the comment above. Owned by that
// environment via env.SetInstanceData(); Node-API deletes it
// automatically when the environment tears down.
struct InstanceData {
  std::mutex logEventsMutex;
  std::vector<LogEvent> logEvents;
  Napi::FunctionReference logHandlerRef;
  bool logHandlerInstalled = false;
};

namespace {

// Set by CurrentInstanceGuard for the duration of each AsyncWorker's
// Execute() call, so LogTrampoline() (which Exiv2 may invoke synchronously,
// inline, from deep inside readMetadata()/writeMetadata()) knows which
// environment's InstanceData a warning belongs to. Thread-local, not just
// a plain global, because several environments' Execute() calls can be
// genuinely running concurrently on different libuv worker-pool threads.
thread_local InstanceData* tlsCurrentInstance = nullptr;

struct CurrentInstanceGuard {
  InstanceData* previous;
  explicit CurrentInstanceGuard(InstanceData* instanceData) : previous(tlsCurrentInstance) {
    tlsCurrentInstance = instanceData;
  }
  ~CurrentInstanceGuard() { tlsCurrentInstance = previous; }
};

const char* LevelToString(int level) {
  switch (level) {
    case Exiv2::LogMsg::debug: return "debug";
    case Exiv2::LogMsg::info:  return "info";
    case Exiv2::LogMsg::warn:  return "warn";
    case Exiv2::LogMsg::error: return "error";
    case Exiv2::LogMsg::mute:  return "mute";
    default: return "warn";
  }
}

bool StringToLevel(const std::string& name, Exiv2::LogMsg::Level& level) {
  if (name == "debug") { level = Exiv2::LogMsg::debug; return true; }
  if (name == "info")  { level = Exiv2::LogMsg::info;  return true; }
  if (name == "warn")  { level = Exiv2::LogMsg::warn;  return true; }
  if (name == "error") { level = Exiv2::LogMsg::error; return true; }
  if (name == "mute")  { level = Exiv2::LogMsg::mute;  return true; }
  return false;
}

// Exiv2::LogMsg::Handler-compatible; installed exactly once, process-wide
// (see InitAll()). May run on a libuv worker-pool thread; only ever
// touches the mutex-protected plain-data buffer on whichever environment's
// InstanceData tlsCurrentInstance currently points to, never Napi::*
// directly.
void LogTrampoline(int level, const char* message) {
  InstanceData* instanceData = tlsCurrentInstance;
  if (instanceData) {
    std::lock_guard<std::mutex> lock(instanceData->logEventsMutex);
    if (instanceData->logHandlerInstalled) {
      instanceData->logEvents.push_back(LogEvent{LevelToString(level), message ? message : ""});
      return;
    }
  }
  // No environment context (shouldn't normally happen -- every Exiv2 call
  // in this addon runs inside some AsyncWorker's Execute(), guarded by
  // CurrentInstanceGuard), or the environment that owns this call never
  // installed a custom handler: fall back to Exiv2's own default so
  // diagnostics are never silently dropped just because *some other*
  // environment elsewhere in the process happens to use setLogHandler().
  Exiv2::LogMsg::defaultHandler(level, message);
}

}  // namespace

// Delivers any log events buffered (via LogTrampoline) since the last
// drain to the installed JS handler, in order, then clears the buffer.
// Must run on the JS thread. Called from every AsyncWorker's OnOK()/
// OnError() below, before it invokes its own callback, so a consumer
// always sees a call's log lines before that call's own result/error
// callback fires.
void DrainLogEvents(Napi::Env env) {
  InstanceData* instanceData = env.GetInstanceData<InstanceData>();

  std::vector<LogEvent> drained;
  {
    std::lock_guard<std::mutex> lock(instanceData->logEventsMutex);
    if (instanceData->logEvents.empty()) {
      return;
    }
    drained.swap(instanceData->logEvents);
  }

  if (!instanceData->logHandlerInstalled) {
    return;
  }

  Napi::HandleScope scope(env);
  for (const LogEvent& e : drained) {
    Napi::Object event = Napi::Object::New(env);
    event.Set("level", Napi::String::New(env, e.level));
    event.Set("message", Napi::String::New(env, e.message));
    try {
      instanceData->logHandlerRef.Call({event});
    } catch (const Napi::Error&) {
      // A handler that throws must not take down the call that triggered
      // it (NAPI_CPP_EXCEPTIONS turns a throwing JS callback into a C++
      // exception here; left uncaught, it would unwind straight out of the
      // calling worker's OnOK()/OnError(), skipping that call's own
      // result/error callback entirely). Drop any remaining events from
      // this drain and let the caller continue -- there's no good channel
      // to report a broken log handler back through other than stderr.
      fprintf(stderr, "@janhapke/exiv2: setLogHandler() callback threw; further diagnostics are being dropped until it's fixed\n");
      break;
    }
  }
}

// setLogLevel()/muteLog() write Exiv2::LogMsg's own static level_ (see
// error.hpp -- a plain, non-atomic static member, with no internal
// protection of any kind, unrelated to the XMP-specific gaps gExiv2Mutex
// otherwise exists for). Every one of Exiv2's internal log macros
// (EXV_WARNING, etc., used throughout its parsing code) reads it,
// unconditionally, on every invocation. So these two take an EXCLUSIVE
// lock (std::unique_lock), not the shared one GetTagsWorker/
// GetPreviewsWorker use -- calling setLogLevel()/muteLog() while any read
// is in flight must still block until that read finishes, otherwise it
// races an unsynchronized write on the main thread against those
// unsynchronized reads on a worker-pool thread. (A consumer that only
// ever calls these once, at startup before issuing any calls, wouldn't
// hit this -- but nothing in the API contract requires that.) Note this
// is still process-wide, same as before (see the log-control comment
// above): Exiv2::LogMsg::level_ has no per-environment concept at all.
//
// setLogHandler(), by contrast, only ever touches this environment's own
// InstanceData (see the log-control comment above) -- it doesn't need
// gExiv2Mutex at all, since Exiv2::LogMsg::handler_ itself is set to
// LogTrampoline exactly once, process-wide, in InitAll(), and never
// changed again per-call.

Napi::Value SetLogLevel(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "Usage: setLogLevel('debug'|'info'|'warn'|'error'|'mute')").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  std::string name = info[0].As<Napi::String>().Utf8Value();
  Exiv2::LogMsg::Level level;
  if (!StringToLevel(name, level)) {
    Napi::TypeError::New(env, "Invalid log level, expected one of: 'debug', 'info', 'warn', 'error', 'mute'").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  std::unique_lock<std::shared_mutex> exiv2Lock(gExiv2Mutex);
  Exiv2::LogMsg::setLevel(level);
  return env.Undefined();
}

Napi::Value MuteLog(const Napi::CallbackInfo& info) {
  std::unique_lock<std::shared_mutex> exiv2Lock(gExiv2Mutex);
  Exiv2::LogMsg::setLevel(Exiv2::LogMsg::mute);
  return info.Env().Undefined();
}

Napi::Value SetLogHandler(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1 || !(info[0].IsFunction() || info[0].IsNull() || info[0].IsUndefined())) {
    Napi::TypeError::New(env, "Usage: setLogHandler(function | null)").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  InstanceData* instanceData = env.GetInstanceData<InstanceData>();

  if (info[0].IsFunction()) {
    instanceData->logHandlerRef = Napi::Persistent(info[0].As<Napi::Function>());
    instanceData->logHandlerInstalled = true;
  } else {
    // null/undefined restores Exiv2's own default (stderr) handler for
    // *this* environment. Note this is not the same as muting:
    // LogMsg::setHandler(nullptr) would suppress messages outright for
    // every environment, per error.hpp's own docs -- suppression is
    // muteLog()/setLogLevel('mute')'s job, and it's still process-wide.
    instanceData->logHandlerInstalled = false;
    instanceData->logHandlerRef.Reset();
    std::lock_guard<std::mutex> lock(instanceData->logEventsMutex);
    instanceData->logEvents.clear();
  }

  return env.Undefined();
}

// - - - GetTagsWorker - - -

class GetTagsWorker : public Napi::AsyncWorker {
 public:
  GetTagsWorker(Napi::Function& callback, const std::string fileName)
    : Napi::AsyncWorker(callback), isBuf(false), fileName(fileName),
      instanceData(callback.Env().GetInstanceData<InstanceData>()) {}

  GetTagsWorker(Napi::Function& callback, const char* buf, const size_t len)
    : Napi::AsyncWorker(callback), isBuf(true), bufLen(len),
      instanceData(callback.Env().GetInstanceData<InstanceData>()) {
    // Copy the buffer data since it may be garbage collected before Execute runs
    bufCopy = new Exiv2::byte[len];
    memcpy(bufCopy, buf, len);
    this->buf = bufCopy;
  }

  ~GetTagsWorker() {
    if (bufCopy) {
      delete[] bufCopy;
    }
  }

  // Executed inside the worker-thread. Not safe to access Napi values here.
  void Execute() override {
    // Read-only (readMetadata() only, never writeMetadata()) -- shared
    // lock, runs concurrently with other reads. See gExiv2Mutex's comment.
    std::shared_lock<std::shared_mutex> exiv2Lock(gExiv2Mutex);
    // Routes any warning Exiv2 emits during this call to this worker's own
    // environment -- see the log-control comment and CurrentInstanceGuard.
    CurrentInstanceGuard logGuard(instanceData);
    try {
      #if USE_EXIV2_UNIQUE_PTR
        Exiv2::Image::UniquePtr image = this->isBuf
          ? Exiv2::ImageFactory::open(this->buf, this->bufLen)
          : Exiv2::ImageFactory::open(this->fileName);
      #else
        Exiv2::Image::AutoPtr image = this->isBuf
          ? Exiv2::ImageFactory::open(this->buf, this->bufLen)
          : Exiv2::ImageFactory::open(this->fileName);
      #endif
      if (!image.get()) {
        exifException = "Failed to open image";
        return;
      }
      image->readMetadata();

      Exiv2::ExifData &exifData = image->exifData();
      if (exifData.empty() == false) {
        Exiv2::ExifData::const_iterator end = exifData.end();
        for (Exiv2::ExifData::const_iterator i = exifData.begin(); i != end; ++i) {
          tags.insert(std::pair<std::string, std::string>(i->key(), i->print(&exifData)));
        }
      }

      Exiv2::IptcData &iptcData = image->iptcData();
      if (iptcData.empty() == false) {
        Exiv2::IptcData::const_iterator end = iptcData.end();
        for (Exiv2::IptcData::const_iterator i = iptcData.begin(); i != end; ++i) {
          tags.insert(std::pair<std::string, std::string>(i->key(), i->print()));
        }
      }

      Exiv2::XmpData &xmpData = image->xmpData();
      if (xmpData.empty() == false) {
        Exiv2::XmpData::const_iterator end = xmpData.end();
        for (Exiv2::XmpData::const_iterator i = xmpData.begin(); i != end; ++i) {
          tags.insert(std::pair<std::string, std::string>(i->key(), i->print()));
        }
      }
    } catch (std::exception& e) {
      exifException = e.what();
    }
  }

  void OnOK() override {
    Napi::HandleScope scope(Env());
    DrainLogEvents(Env());

    if (!exifException.empty()) {
      Callback().Call({Napi::String::New(Env(), exifException), Env().Null()});
    } else if (!tags.empty()) {
      Napi::Object hash = Napi::Object::New(Env());
      for (tag_map_t::iterator i = tags.begin(); i != tags.end(); ++i) {
        hash.Set(i->first, i->second);
      }
      Callback().Call({Env().Null(), hash});
    } else {
      Callback().Call({Env().Null(), Env().Null()});
    }
  }

  void OnError(const Napi::Error& e) override {
    Napi::HandleScope scope(Env());
    DrainLogEvents(Env());
    AsyncWorker::OnError(e);
  }

 protected:
  const bool isBuf;
  const Exiv2::byte* buf = nullptr;
  Exiv2::byte* bufCopy = nullptr;
  const size_t bufLen = 0;
  const std::string fileName = "";
  InstanceData* instanceData;
  std::string exifException;
  tag_map_t tags;
};

Napi::Value GetImageTags(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() <= 1 || !info[1].IsFunction()) {
    Napi::TypeError::New(env, "Usage: <filename/buffer> <callback function>").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  Napi::Function callback = info[1].As<Napi::Function>();

  if (info[0].IsString()) {
    std::string filename = info[0].As<Napi::String>().Utf8Value();
    GetTagsWorker* worker = new GetTagsWorker(callback, filename);
    worker->Queue();
  } else if (info[0].IsBuffer()) {
    Napi::Buffer<char> buf = info[0].As<Napi::Buffer<char>>();
    GetTagsWorker* worker = new GetTagsWorker(callback, buf.Data(), buf.Length());
    worker->Queue();
  } else {
    Napi::TypeError::New(env, "First argument must be a string or buffer").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  return env.Undefined();
}

// - - - SetTagsWorker - - -

class SetTagsWorker : public Napi::AsyncWorker {
 public:
  SetTagsWorker(Napi::Function& callback, const std::string fileName, tag_map_t tags)
    : Napi::AsyncWorker(callback), isBuf(false), fileName(fileName), tags(tags),
      instanceData(callback.Env().GetInstanceData<InstanceData>()) {}

  SetTagsWorker(Napi::Function& callback, const char* buf, const size_t len, tag_map_t tags)
    : Napi::AsyncWorker(callback), isBuf(true), bufLen(len), tags(tags),
      instanceData(callback.Env().GetInstanceData<InstanceData>()) {
    // Copy the buffer data since it may be garbage collected before Execute runs
    bufCopy = new Exiv2::byte[len];
    memcpy(bufCopy, buf, len);
    this->buf = bufCopy;
  }

  ~SetTagsWorker() {
    if (bufCopy) {
      delete[] bufCopy;
    }
  }

  void Execute() override {
    // Calls writeMetadata() -- exclusive lock. See gExiv2Mutex's comment
    // for why writes can't safely share the lock reads use.
    std::unique_lock<std::shared_mutex> exiv2Lock(gExiv2Mutex);
    // Routes any warning Exiv2 emits during this call to this worker's own
    // environment -- see the log-control comment and CurrentInstanceGuard.
    CurrentInstanceGuard logGuard(instanceData);
    try {
      #if USE_EXIV2_UNIQUE_PTR
        Exiv2::Image::UniquePtr image = this->isBuf
          ? Exiv2::ImageFactory::open(this->buf, this->bufLen)
          : Exiv2::ImageFactory::open(this->fileName);
      #else
        Exiv2::Image::AutoPtr image = this->isBuf
          ? Exiv2::ImageFactory::open(this->buf, this->bufLen)
          : Exiv2::ImageFactory::open(this->fileName);
      #endif
      if (!image.get()) {
        exifException = "Failed to open image";
        return;
      }

      image->readMetadata();
      Exiv2::ExifData &exifData = image->exifData();
      Exiv2::IptcData &iptcData = image->iptcData();
      Exiv2::XmpData &xmpData = image->xmpData();

      // Assign the tags.
      for (tag_map_t::iterator i = tags.begin(); i != tags.end(); ++i) {
        if (i->first.compare(0, 5, "Exif.") == 0) {
          exifData[i->first].setValue(i->second);
        } else if (i->first.compare(0, 5, "Iptc.") == 0) {
          iptcData[i->first].setValue(i->second);
        } else if (i->first.compare(0, 4, "Xmp.") == 0) {
          xmpData[i->first].setValue(i->second);
        }
      }

      // Write the tag data the image file.
      image->setExifData(exifData);
      image->setIptcData(iptcData);
      image->setXmpData(xmpData);
      image->writeMetadata();
    } catch (std::exception& e) {
      exifException = e.what();
    }
  }

  void OnOK() override {
    Napi::HandleScope scope(Env());
    DrainLogEvents(Env());

    if (!exifException.empty()) {
      Callback().Call({Napi::String::New(Env(), exifException)});
    } else {
      Callback().Call({Env().Null()});
    }
  }

  void OnError(const Napi::Error& e) override {
    Napi::HandleScope scope(Env());
    DrainLogEvents(Env());
    AsyncWorker::OnError(e);
  }

 protected:
  const bool isBuf;
  const Exiv2::byte* buf = nullptr;
  Exiv2::byte* bufCopy = nullptr;
  const size_t bufLen = 0;
  const std::string fileName = "";
  std::string exifException;
  tag_map_t tags;
  InstanceData* instanceData;
};

Napi::Value SetImageTags(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() <= 2 || !info[2].IsFunction()) {
    Napi::TypeError::New(env, "Usage: <filename/buffer> <tags> <callback function>").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  Napi::Function callback = info[2].As<Napi::Function>();

  // Validate second argument is an object
  if (!info[1].IsObject() || info[1].IsArray()) {
    Napi::TypeError::New(env, "Second argument must be an object of tags").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  // Extract tags from the second argument
  tag_map_t tags;
  Napi::Object tagsObj = info[1].As<Napi::Object>();
  Napi::Array keys = tagsObj.GetPropertyNames();
  for (uint32_t i = 0; i < keys.Length(); i++) {
    Napi::Value key = keys.Get(i);
    Napi::Value value = tagsObj.Get(key);

    // Coerce values to strings
    std::string keyStr = key.As<Napi::String>().Utf8Value();
    std::string valueStr;
    if (value.IsString()) {
      valueStr = value.As<Napi::String>().Utf8Value();
    } else if (value.IsNumber()) {
      double d = value.As<Napi::Number>().DoubleValue();
      // Format integers without decimal places
      if (std::isfinite(d) && d == std::floor(d)) {
        valueStr = std::to_string(static_cast<int64_t>(d));
      } else {
        valueStr = std::to_string(d);
      }
    } else {
      // Try to coerce to string via ToString()
      valueStr = value.ToString().Utf8Value();
    }
    tags.insert(std::pair<std::string, std::string>(keyStr, valueStr));
  }

  if (info[0].IsString()) {
    std::string filename = info[0].As<Napi::String>().Utf8Value();
    SetTagsWorker* worker = new SetTagsWorker(callback, filename, tags);
    worker->Queue();
  } else if (info[0].IsBuffer()) {
    Napi::Buffer<char> buf = info[0].As<Napi::Buffer<char>>();
    SetTagsWorker* worker = new SetTagsWorker(callback, buf.Data(), buf.Length(), tags);
    worker->Queue();
  } else {
    Napi::TypeError::New(env, "First argument must be a string or buffer").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  return env.Undefined();
}

// - - - DeleteTagsWorker - - -

class DeleteTagsWorker : public Napi::AsyncWorker {
 public:
  DeleteTagsWorker(Napi::Function& callback, const std::string fileName, std::vector<std::string> tags)
    : Napi::AsyncWorker(callback), isBuf(false), fileName(fileName), tags(tags),
      instanceData(callback.Env().GetInstanceData<InstanceData>()) {}

  DeleteTagsWorker(Napi::Function& callback, const char* buf, const size_t len, std::vector<std::string> tags)
    : Napi::AsyncWorker(callback), isBuf(true), bufLen(len), tags(tags),
      instanceData(callback.Env().GetInstanceData<InstanceData>()) {
    // Copy the buffer data since it may be garbage collected before Execute runs
    bufCopy = new Exiv2::byte[len];
    memcpy(bufCopy, buf, len);
    this->buf = bufCopy;
  }

  ~DeleteTagsWorker() {
    if (bufCopy) {
      delete[] bufCopy;
    }
  }

  void Execute() override {
    // Calls writeMetadata() -- exclusive lock, same reasoning as
    // SetTagsWorker::Execute().
    std::unique_lock<std::shared_mutex> exiv2Lock(gExiv2Mutex);
    // Routes any warning Exiv2 emits during this call to this worker's own
    // environment -- see the log-control comment and CurrentInstanceGuard.
    CurrentInstanceGuard logGuard(instanceData);
    try {
      #if USE_EXIV2_UNIQUE_PTR
        Exiv2::Image::UniquePtr image = this->isBuf
          ? Exiv2::ImageFactory::open(this->buf, this->bufLen)
          : Exiv2::ImageFactory::open(this->fileName);
      #else
        Exiv2::Image::AutoPtr image = this->isBuf
          ? Exiv2::ImageFactory::open(this->buf, this->bufLen)
          : Exiv2::ImageFactory::open(this->fileName);
      #endif
      if (!image.get()) {
        exifException = "Failed to open image";
        return;
      }

      image->readMetadata();
      Exiv2::ExifData &exifData = image->exifData();
      Exiv2::IptcData &iptcData = image->iptcData();
      Exiv2::XmpData &xmpData = image->xmpData();

      // Erase the tags.
      for (std::vector<std::string>::iterator i = tags.begin(); i != tags.end(); ++i) {
        if (i->compare(0, 5, "Exif.") == 0) {
          Exiv2::ExifKey k(*i);
          Exiv2::ExifData::iterator pos = exifData.findKey(k);
          if (pos != exifData.end()) {
            exifData.erase(pos);
          }
        } else if (i->compare(0, 5, "Iptc.") == 0) {
          Exiv2::IptcKey k(*i);
          Exiv2::IptcData::iterator pos = iptcData.findKey(k);
          if (pos != iptcData.end()) {
            iptcData.erase(pos);
          }
        } else if (i->compare(0, 4, "Xmp.") == 0) {
          Exiv2::XmpKey k(*i);
          Exiv2::XmpData::iterator pos = xmpData.findKey(k);
          if (pos != xmpData.end()) {
            xmpData.erase(pos);
          }
        }
      }

      // Write the tag data the image file.
      image->setExifData(exifData);
      image->setIptcData(iptcData);
      image->setXmpData(xmpData);
      image->writeMetadata();
    } catch (std::exception& e) {
      exifException = e.what();
    }
  }

  void OnOK() override {
    Napi::HandleScope scope(Env());
    DrainLogEvents(Env());

    if (!exifException.empty()) {
      Callback().Call({Napi::String::New(Env(), exifException)});
    } else {
      Callback().Call({Env().Null()});
    }
  }

  void OnError(const Napi::Error& e) override {
    Napi::HandleScope scope(Env());
    DrainLogEvents(Env());
    AsyncWorker::OnError(e);
  }

 protected:
  const bool isBuf;
  const Exiv2::byte* buf = nullptr;
  Exiv2::byte* bufCopy = nullptr;
  const size_t bufLen = 0;
  const std::string fileName = "";
  std::string exifException;
  std::vector<std::string> tags;
  InstanceData* instanceData;
};

Napi::Value DeleteImageTags(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() <= 2 || !info[2].IsFunction()) {
    Napi::TypeError::New(env, "Usage: <filename/buffer> <tags> <callback function>").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  Napi::Function callback = info[2].As<Napi::Function>();

  // Validate second argument is an array
  if (!info[1].IsArray()) {
    Napi::TypeError::New(env, "Second argument must be an array of tag names").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  // Extract tags from the second argument (array of tag names to delete)
  std::vector<std::string> tags;
  Napi::Array keysArray = info[1].As<Napi::Array>();
  for (uint32_t i = 0; i < keysArray.Length(); i++) {
    Napi::Value key = keysArray.Get(i);
    // Handle non-string values gracefully
    if (key.IsString()) {
      tags.push_back(key.As<Napi::String>().Utf8Value());
    } else {
      // Coerce to string
      tags.push_back(key.ToString().Utf8Value());
    }
  }

  if (info[0].IsString()) {
    std::string filename = info[0].As<Napi::String>().Utf8Value();
    DeleteTagsWorker* worker = new DeleteTagsWorker(callback, filename, tags);
    worker->Queue();
  } else if (info[0].IsBuffer()) {
    Napi::Buffer<char> buf = info[0].As<Napi::Buffer<char>>();
    DeleteTagsWorker* worker = new DeleteTagsWorker(callback, buf.Data(), buf.Length(), tags);
    worker->Queue();
  } else {
    Napi::TypeError::New(env, "First argument must be a string or buffer").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  return env.Undefined();
}

// - - - GetPreviewsWorker - - -

class GetPreviewsWorker : public Napi::AsyncWorker {
 public:
  GetPreviewsWorker(Napi::Function& callback, const std::string fileName)
    : Napi::AsyncWorker(callback), isBuf(false), fileName(fileName),
      instanceData(callback.Env().GetInstanceData<InstanceData>()) {}

  GetPreviewsWorker(Napi::Function& callback, const char* buf, const size_t len)
    : Napi::AsyncWorker(callback), isBuf(true), bufLen(len),
      instanceData(callback.Env().GetInstanceData<InstanceData>()) {
    // Copy the buffer data since it may be garbage collected before Execute runs
    bufCopy = new Exiv2::byte[len];
    memcpy(bufCopy, buf, len);
    this->buf = bufCopy;
  }

  ~GetPreviewsWorker() {
    if (bufCopy) {
      delete[] bufCopy;
    }
  }

  void Execute() override {
    // Read-only (readMetadata() + preview extraction, never
    // writeMetadata()) -- shared lock, same reasoning as
    // GetTagsWorker::Execute().
    std::shared_lock<std::shared_mutex> exiv2Lock(gExiv2Mutex);
    // Routes any warning Exiv2 emits during this call to this worker's own
    // environment -- see the log-control comment and CurrentInstanceGuard.
    CurrentInstanceGuard logGuard(instanceData);
    try {
      #if USE_EXIV2_UNIQUE_PTR
        Exiv2::Image::UniquePtr image = this->isBuf
          ? Exiv2::ImageFactory::open(this->buf, this->bufLen)
          : Exiv2::ImageFactory::open(this->fileName);
      #else
        Exiv2::Image::AutoPtr image = this->isBuf
          ? Exiv2::ImageFactory::open(this->buf, this->bufLen)
          : Exiv2::ImageFactory::open(this->fileName);
      #endif
      if (!image.get()) {
        exifException = "Failed to open image";
        return;
      }
      image->readMetadata();

      Exiv2::PreviewManager manager(*image);
      Exiv2::PreviewPropertiesList list = manager.getPreviewProperties();

      for (Exiv2::PreviewPropertiesList::iterator pos = list.begin(); pos != list.end(); pos++) {
        Exiv2::PreviewImage previewImage = manager.getPreviewImage(*pos);

        previews.emplace_back(previewImage.mimeType(), previewImage.height(),
          previewImage.width(), (char*)previewImage.pData(), previewImage.size());
      }
    } catch (std::exception& e) {
      exifException = e.what();
    }
  }

  void OnOK() override {
    Napi::HandleScope scope(Env());
    DrainLogEvents(Env());

    if (!exifException.empty()) {
      Callback().Call({Napi::String::New(Env(), exifException), Env().Null()});
    } else {
      Napi::Array array = Napi::Array::New(Env(), previews.size());
      for (size_t i = 0; i < previews.size(); ++i) {
        Napi::Object preview = Napi::Object::New(Env());
        preview.Set("mimeType", previews[i].mimeType);
        preview.Set("height", Napi::Number::New(Env(), previews[i].height));
        preview.Set("width", Napi::Number::New(Env(), previews[i].width));
        preview.Set("data", Napi::Buffer<char>::Copy(Env(), previews[i].data, previews[i].size));
        array.Set(i, preview);
      }
      Callback().Call({Env().Null(), array});
    }
  }

  void OnError(const Napi::Error& e) override {
    Napi::HandleScope scope(Env());
    DrainLogEvents(Env());
    AsyncWorker::OnError(e);
  }

 protected:
  struct Preview {
    // Constructor
    Preview(std::string type_, uint32_t height_, uint32_t width_, const char *data_, size_t size_)
      : mimeType(std::move(type_)), height(height_), width(width_), size(size_), data(nullptr) {
      if (size > 0 && data_) {
        data = new char[size];
        memcpy(data, data_, size);
      }
    }

    // Delete copy constructor and copy assignment (Rule of Five)
    Preview(const Preview&) = delete;
    Preview& operator=(const Preview&) = delete;

    // Move constructor
    Preview(Preview&& other) noexcept
      : mimeType(std::move(other.mimeType)), height(other.height), width(other.width),
        size(other.size), data(other.data) {
      other.data = nullptr;
      other.size = 0;
    }

    // Move assignment operator
    Preview& operator=(Preview&& other) noexcept {
      if (this != &other) {
        delete[] data;
        mimeType = std::move(other.mimeType);
        height = other.height;
        width = other.width;
        size = other.size;
        data = other.data;
        other.data = nullptr;
        other.size = 0;
      }
      return *this;
    }

    // Destructor
    ~Preview() {
      delete[] data;
    }

    std::string mimeType;
    uint32_t height;
    uint32_t width;
    size_t size;
    char* data;
  };

  const bool isBuf;
  const Exiv2::byte* buf = nullptr;
  Exiv2::byte* bufCopy = nullptr;
  const size_t bufLen = 0;
  const std::string fileName = "";
  std::string exifException;
  std::vector<Preview> previews;
  InstanceData* instanceData;
};

Napi::Value GetImagePreviews(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() <= 1 || !info[1].IsFunction()) {
    Napi::TypeError::New(env, "Usage: <filename/buffer> <callback function>").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  Napi::Function callback = info[1].As<Napi::Function>();

  if (info[0].IsString()) {
    std::string filename = info[0].As<Napi::String>().Utf8Value();
    GetPreviewsWorker* worker = new GetPreviewsWorker(callback, filename);
    worker->Queue();
  } else if (info[0].IsBuffer()) {
    Napi::Buffer<char> buf = info[0].As<Napi::Buffer<char>>();
    GetPreviewsWorker* worker = new GetPreviewsWorker(callback, buf.Data(), buf.Length());
    worker->Queue();
  } else {
    Napi::TypeError::New(env, "First argument must be a string or buffer").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  return env.Undefined();
}

// - - - Module Init - - -

namespace {
// How many live Node.js environments (main thread + every worker_thread
// that has require()'d this addon and not yet torn down) currently exist
// in this process. Guarded by gExiv2Mutex. See the log-control comment
// above for why this addon -- a Node-API addon -- gets InitAll() called
// once per environment rather than once per process (issue #6), and why
// that means XmpParser::initialize()/terminate() and installing
// LogTrampoline as Exiv2's log handler must happen exactly once each, at
// the first environment's setup and the last environment's teardown, not
// once per environment: both are genuinely process-wide Exiv2 state, and
// tearing them down while another environment is still actively using
// Exiv2 would pull the toolkit out from under it mid-call.
int gEnvironmentCount = 0;
}  // namespace

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  env.SetInstanceData(new InstanceData());

  {
    std::unique_lock<std::shared_mutex> exiv2Lock(gExiv2Mutex);
    if (gEnvironmentCount == 0) {
      // First environment in the process to load this addon. Runs
      // synchronously, before this environment's own worker pool could
      // possibly have anything queued on it, so this is inherently the
      // "thread-safe manner (e.g. on program startup, before threads are
      // created)" XmpParser::initialize()'s own docs call for. It closes
      // the one unprotected lazy-init gap in Exiv2's read path (see
      // gExiv2Mutex's comment): without this, the first Execute() call
      // that happens to need XMP would trigger this same initialize()
      // call lazily and unprotected, racing any other worker-pool thread
      // hitting it at the same time. Installing LogTrampoline here too
      // (rather than per setLogHandler() call, as a previous version of
      // this file did) means Exiv2::LogMsg::handler_ never needs to
      // change again for the rest of the process's life -- LogTrampoline
      // itself routes each call to the right environment, or falls back
      // to defaultHandler, per InstanceData (see the log-control comment
      // above).
      Exiv2::XmpParser::initialize();
      Exiv2::LogMsg::setHandler(&LogTrampoline);
    }
    gEnvironmentCount++;
  }

  exports.Set("getImageTags", Napi::Function::New(env, GetImageTags));
  exports.Set("setImageTags", Napi::Function::New(env, SetImageTags));
  exports.Set("deleteImageTags", Napi::Function::New(env, DeleteImageTags));
  exports.Set("getImagePreviews", Napi::Function::New(env, GetImagePreviews));
  exports.Set("setLogLevel", Napi::Function::New(env, SetLogLevel));
  exports.Set("muteLog", Napi::Function::New(env, MuteLog));
  exports.Set("setLogHandler", Napi::Function::New(env, SetLogHandler));

  env.AddCleanupHook([]() {
    std::unique_lock<std::shared_mutex> exiv2Lock(gExiv2Mutex);
    gEnvironmentCount--;
    if (gEnvironmentCount == 0) {
      // Last environment in the process tearing down -- safe to undo the
      // process-wide setup above, matching counterparts to the calls in
      // the gEnvironmentCount == 0 branch. (This environment's own
      // InstanceData -- including any handler it had installed -- is
      // torn down separately and automatically by Node-API, since it was
      // registered via env.SetInstanceData() above.)
      Exiv2::LogMsg::setHandler(&Exiv2::LogMsg::defaultHandler);
      Exiv2::XmpParser::terminate();
    }
  });

  return exports;
}

NODE_API_MODULE(exiv2, InitAll)
