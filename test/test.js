
// Make sure the date tests always use the UTC timezone
process.env.TZ = 'UTC';

const exiv = require('../exiv2')
  , fs = require('fs')
  , util = require('util')
  , should = require('should')
  , spawnSync = require('child_process').spawnSync
  , dir = __dirname + '/images';


describe('exiv2', function(){
  describe('.getImageTags()', function(){
    it("should callback with image's tags", function(done) {
      exiv.getImageTags(dir + '/books.jpg', function(err, tags) {
        should.not.exist(err);

        tags.should.have.property('Exif.Image.DateTime', '2008:12:16 21:28:36');
        tags.should.have.property('Exif.Photo.DateTimeOriginal', '2008:12:16 21:28:36');
        done();
      })
    });

    it("should callback with image's tags when called with a buffer", function(done) {
      exiv.getImageTags(fs.readFileSync(dir + '/books.jpg'), function(err, tags) {
        should.not.exist(err);

        tags.should.have.property('Exif.Image.DateTime', '2008:12:16 21:28:36');
        tags.should.have.property('Exif.Photo.DateTimeOriginal', '2008:12:16 21:28:36');
        done();
      })
    });

    it('should callback with null on untagged file', function(done) {
      exiv.getImageTags(dir + '/damien.jpg', function(err, tags) {
        should.not.exist(err);
        should.not.exist(tags);
        done();
      })
    });

    it('should callback with null on untagged buffer', function(done) {
      exiv.getImageTags(fs.readFileSync(dir + '/damien.jpg'), function(err, tags) {
        should.not.exist(err);
        should.not.exist(tags);
        done();
      })
    });

    it('should throw if no file path or buffer is provided', function() {
      (function(){
        exiv.getImageTags()
      }).should.throw();
    });

    it('should throw if no callback is provided', function() {
      (function(){
        exiv.getImageTags(dir + '/books.jpg')
      }).should.throw();
    });

    it('should report an error on an invalid path', function(done) {
      exiv.getImageTags('idontexist.jpg', function(err, tags) {
        should.exist(err);
        should.not.exist(tags);
        done();
      });
    });

    it('should report an error on an empty buffer', function(done) {
      exiv.getImageTags(Buffer.alloc(0), function(err, tags) {
        should.exist(err);
        should.not.exist(tags);
        done();
      });
    });

  });

  describe('.setImageTags()', function(){
    var temp = dir + '/copy.jpg';

    before(function() {
      fs.writeFileSync(temp, fs.readFileSync(dir + '/books.jpg'));
    });
    it('should write tags to image files', function(done) {
      var tags = {
        "Exif.Photo.UserComment" : "Some books..",
        "Exif.Canon.OwnerName" : "Sørens kamera",
        "Iptc.Application2.RecordVersion" : "2",
        "Xmp.dc.subject" : "A camera"
      };
      exiv.setImageTags(temp, tags, function(err){
        should.not.exist(err);

        exiv.getImageTags(temp, function(err, tags) {
          tags.should.have.property('Exif.Photo.UserComment', "Some books..");
          tags.should.have.property('Exif.Canon.OwnerName', "Sørens kamera");
          tags.should.have.property('Iptc.Application2.RecordVersion', "2");
          tags.should.have.property('Xmp.dc.subject', "A camera");
          done();
        });
      });
    })
    after(function(done) {
      fs.unlink(temp, done);
    });


    it('should throw if no file path is provided', function() {
      (function(){
        exiv.setImageTags()
      }).should.throw();
    });

    it('should throw if no callback is provided', function() {
      (function(){
        exiv.setImageTags(dir + '/books.jpg')
      }).should.throw();
    });

    it('should report an error on an invalid path', function(done) {
      exiv.setImageTags('idontexist.jpg', {}, function(err, tags) {
        should.exist(err);
        should.not.exist(tags);
        done();
      });
    });

    it('should throw if tags is not an object', function() {
      (function(){
        exiv.setImageTags(dir + '/books.jpg', ['not', 'an', 'object'], function(){})
      }).should.throw(/must be an object/);
    });
  });

  describe('.deleteImageTags()', function(){
    var temp = dir + '/copy-deltags.jpg';

    before(function() {
      fs.writeFileSync(temp, fs.readFileSync(dir + '/books.jpg'));
    });
    it('should delete tags in image files', function(done) {
      var tags = ["Exif.Canon.OwnerName"];
      exiv.deleteImageTags(temp, tags, function(err){
        should.not.exist(err);

        exiv.getImageTags(temp, function(err, tags) {
          should.not.exist(err);
          tags.should.not.have.property('Exif.Canon.OwnerName');
          done();
        });
      });
    })
    after(function(done) {
      fs.unlink(temp, done);
    });

    it('should throw if tags is not an array', function() {
      (function(){
        exiv.deleteImageTags(dir + '/books.jpg', {'not': 'an array'}, function(){})
      }).should.throw(/must be an array/);
    });
  });

  describe('.getImagePreviews()', function(){
    it("should callback with image's previews", function(done) {
      exiv.getImagePreviews(dir + '/books.jpg', function(err, previews) {
        should.not.exist(err);
        previews.should.be.an.instanceof(Array);
        previews.should.have.lengthOf(1);
        previews[0].should.have.property('mimeType', 'image/jpeg');
        previews[0].should.have.property('height', 120);
        previews[0].should.have.property('width', 160);
        previews[0].should.have.property('data').with.instanceof(Buffer);
        previews[0].data.should.have.property('length', 6071);
        done();
      });
    });

    it("should callback with image's previews when called with a buffer", function(done) {
      exiv.getImagePreviews(fs.readFileSync(dir + '/books.jpg'), function(err, previews) {
        should.not.exist(err);
        previews.should.be.an.instanceof(Array);
        previews.should.have.lengthOf(1);
        previews[0].should.have.property('mimeType', 'image/jpeg');
        previews[0].should.have.property('height', 120);
        previews[0].should.have.property('width', 160);
        previews[0].should.have.property('data').with.instanceof(Buffer);
        previews[0].data.should.have.property('length', 6071);
        done();
      });
    });

    it('should callback with an empty array for files with no previews', function(done) {
      exiv.getImagePreviews(dir + '/damien.jpg', function(err, previews) {
        should.not.exist(err);
        previews.should.be.an.instanceof(Array);
        previews.should.have.lengthOf(0);
        done();
      })
    });

    it('should callback with an empty array for buffers with no previews', function(done) {
      exiv.getImagePreviews(fs.readFileSync(dir + '/damien.jpg'), function(err, previews) {
        should.not.exist(err);
        previews.should.be.an.instanceof(Array);
        previews.should.have.lengthOf(0);
        done();
      })
    });


    it('should throw if no file path is provided', function() {
      (function(){
        exiv.getImagePreviews()
      }).should.throw();
    });

    it('should throw if no callback is provided', function() {
      (function(){
        exiv.getImagePreviews(dir + '/books.jpg')
      }).should.throw();
    });

    it('should report an error on an invalid path', function(done) {
      exiv.getImagePreviews('idontexist.jpg', function(err, previews) {
        should.exist(err);
        should.not.exist(previews);
        done();
      });
    });
  });

  describe('log control', function() {
    // Exiv2::LogMsg writes its own diagnostics (malformed TIFF/IFD structure,
    // etc.) straight to stderr, bypassing getImageTags()'s callback entirely
    // (see issue #1). corrupt-ifd.jpg is books.jpg with its IFD0 entry count
    // patched to a bogus 572, which reliably reproduces that: Exiv2 logs
    // "Directory Image with 572 entries considered invalid; not read." and
    // getImageTags() still resolves with no tags and no error.
    //
    // Every case here runs in a subprocess -- see
    // test/helpers/log-control-subprocess.js's header comment for why
    // (Exiv2's default handler bypasses Node's stderr stream wrapper, and
    // setLogLevel()/setLogHandler() are process-global state that must not
    // leak between test cases).
    var corruptFixture = dir + '/corrupt-ifd.jpg';
    var helper = __dirname + '/helpers/log-control-subprocess.js';

    function run(mode) {
      return spawnSync(process.execPath, [helper, mode, corruptFixture]);
    }

    it('logs to stderr by default when Exiv2 hits malformed IFD structure', function() {
      var result = run('default');
      result.stderr.toString().should.match(/considered invalid/);
    });

    it("muteLog() suppresses the diagnostic entirely", function() {
      var result = run('mute');
      result.stderr.toString().should.equal('');
    });

    it("setLogLevel('mute') suppresses the diagnostic entirely", function() {
      var result = run('mute-via-level');
      result.stderr.toString().should.equal('');
    });

    it('setLogHandler(cb) receives {level, message} and stderr stays silent', function() {
      var result = run('handler');
      result.stderr.toString().should.equal('');

      var events = JSON.parse(result.stdout.toString());
      events.should.have.lengthOf(1);
      events[0].should.have.property('level', 'error');
      events[0].message.should.match(/considered invalid/);
    });

    it('setLogHandler(null) restores the default stderr handler', function() {
      var result = run('handler-then-clear');
      result.stderr.toString().should.match(/considered invalid/);
    });

    it('should throw synchronously for an invalid log level', function() {
      (function(){
        exiv.setLogLevel('bogus');
      }).should.throw(/Invalid log level/);
    });

    it('should throw synchronously if no log level is provided', function() {
      (function(){
        exiv.setLogLevel();
      }).should.throw();
    });

    it('should throw synchronously for an invalid log handler', function() {
      (function(){
        exiv.setLogHandler(123);
      }).should.throw(/Usage: setLogHandler/);
    });
  });

  describe('.getDate()', function() {
    var tags = {'Exif.Photo.DateTimeOriginal': '2012:04:14 17:45:52'};

    it("should return a false value if the tag doesn't exist", function() {
      var d = exiv.getDate({});
      should.not.exist(d);
    });

    it("should return a Date", function() {
      var d = exiv.getDate(tags);
      should.ok(d instanceof Date);
    });

    it("should be the correct date and time", function() {
      var d = exiv.getDate(tags);
      d.toISOString().should.equal('2012-04-14T17:45:52.000Z');
    });

    it("should include milliseconds if available", function() {
      tags['Exif.Photo.SubSecTimeOriginal'] = '99';
      var d = exiv.getDate(tags);
      d.toISOString().should.equal('2012-04-14T17:45:52.990Z');
    });

    it("should use custom tags", function() {
      tags['Exif.Photo.DateTimeDigitized'] = '2012:04:14 17:08:08';
      tags['Exif.Photo.SubSecTimeDigitized'] = '88';
      var d = exiv.getDate(tags, 'Exif.Photo.DateTimeDigitized');
      d.toISOString().should.equal('2012-04-14T17:08:08.880Z');
    });
  })
})
