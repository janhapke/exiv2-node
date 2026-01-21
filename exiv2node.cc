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
#include <vector>
#include <cmath>
#include <exception>
#include <exiv2/image.hpp>
#include <exiv2/exif.hpp>
#include <exiv2/preview.hpp>

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

// - - - GetTagsWorker - - -

class GetTagsWorker : public Napi::AsyncWorker {
 public:
  GetTagsWorker(Napi::Function& callback, const std::string fileName)
    : Napi::AsyncWorker(callback), isBuf(false), fileName(fileName) {}

  GetTagsWorker(Napi::Function& callback, const char* buf, const size_t len)
    : Napi::AsyncWorker(callback), isBuf(true), bufLen(len) {
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
          tags.insert(std::pair<std::string, std::string>(i->key(), i->value().toString()));
        }
      }

      Exiv2::IptcData &iptcData = image->iptcData();
      if (iptcData.empty() == false) {
        Exiv2::IptcData::const_iterator end = iptcData.end();
        for (Exiv2::IptcData::const_iterator i = iptcData.begin(); i != end; ++i) {
          tags.insert(std::pair<std::string, std::string>(i->key(), i->value().toString()));
        }
      }

      Exiv2::XmpData &xmpData = image->xmpData();
      if (xmpData.empty() == false) {
        Exiv2::XmpData::const_iterator end = xmpData.end();
        for (Exiv2::XmpData::const_iterator i = xmpData.begin(); i != end; ++i) {
          tags.insert(std::pair<std::string, std::string>(i->key(), i->value().toString()));
        }
      }
    } catch (std::exception& e) {
      exifException = e.what();
    }
  }

  void OnOK() override {
    Napi::HandleScope scope(Env());

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

 protected:
  const bool isBuf;
  const Exiv2::byte* buf = nullptr;
  Exiv2::byte* bufCopy = nullptr;
  const size_t bufLen = 0;
  const std::string fileName = "";
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
    : Napi::AsyncWorker(callback), isBuf(false), fileName(fileName), tags(tags) {}

  SetTagsWorker(Napi::Function& callback, const char* buf, const size_t len, tag_map_t tags)
    : Napi::AsyncWorker(callback), isBuf(true), bufLen(len), tags(tags) {
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

    if (!exifException.empty()) {
      Callback().Call({Napi::String::New(Env(), exifException)});
    } else {
      Callback().Call({Env().Null()});
    }
  }

 protected:
  const bool isBuf;
  const Exiv2::byte* buf = nullptr;
  Exiv2::byte* bufCopy = nullptr;
  const size_t bufLen = 0;
  const std::string fileName = "";
  std::string exifException;
  tag_map_t tags;
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
    : Napi::AsyncWorker(callback), isBuf(false), fileName(fileName), tags(tags) {}

  DeleteTagsWorker(Napi::Function& callback, const char* buf, const size_t len, std::vector<std::string> tags)
    : Napi::AsyncWorker(callback), isBuf(true), bufLen(len), tags(tags) {
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

    if (!exifException.empty()) {
      Callback().Call({Napi::String::New(Env(), exifException)});
    } else {
      Callback().Call({Env().Null()});
    }
  }

 protected:
  const bool isBuf;
  const Exiv2::byte* buf = nullptr;
  Exiv2::byte* bufCopy = nullptr;
  const size_t bufLen = 0;
  const std::string fileName = "";
  std::string exifException;
  std::vector<std::string> tags;
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
    : Napi::AsyncWorker(callback), isBuf(false), fileName(fileName) {}

  GetPreviewsWorker(Napi::Function& callback, const char* buf, const size_t len)
    : Napi::AsyncWorker(callback), isBuf(true), bufLen(len) {
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

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  exports.Set("getImageTags", Napi::Function::New(env, GetImageTags));
  exports.Set("setImageTags", Napi::Function::New(env, SetImageTags));
  exports.Set("deleteImageTags", Napi::Function::New(env, DeleteImageTags));
  exports.Set("getImagePreviews", Napi::Function::New(env, GetImagePreviews));
  return exports;
}

NODE_API_MODULE(exiv2, InitAll)
