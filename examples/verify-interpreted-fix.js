#!/usr/bin/env node
'use strict';

/**
 * Manual verification script for the raw-vs-interpreted tag fix.
 *
 * Builds the addon are NOT toggled automatically here; this script just
 * loads the currently-built `exiv2.node` and prints the fields that the
 * fix affects. To see the "before" behaviour, `git stash`, rebuild
 * (`npm run build`), run this script, then `git stash pop` and rebuild
 * again to see "after".
 *
 * Usage:
 *   node test/verify-interpreted-fix.js /path/to/photo.NEF [more files...]
 *
 * With no arguments it falls back to any Nikon/Pentax file it can find
 * under the current user's home Pictures directory, since the bug is
 * about manufacturer-specific interpretation (lens IDs, shutter counts)
 * that generic sample JPEGs (e.g. test/images/*.jpg) don't exercise.
 */

const ex = require('../exiv2.js');
const path = require('path');
const { execSync } = require('child_process');

function findSampleFile() {
  try {
    const out = execSync(
      `find "${process.env.HOME}/Pictures" -iname "*.nef" -o -iname "*.pef" 2>/dev/null | head -1`,
      { encoding: 'utf8' }
    ).trim();
    return out || null;
  } catch (e) {
    return null;
  }
}

const files = process.argv.slice(2);
if (files.length === 0) {
  const sample = findSampleFile();
  if (sample) {
    files.push(sample);
  } else {
    console.error('No files given and no sample .NEF/.PEF found under ~/Pictures.');
    console.error('Usage: node test/verify-interpreted-fix.js <file> [file...]');
    process.exit(1);
  }
}

const LENS_ID_KEYS = ['Exif.NikonLd1.LensIDNumber', 'Exif.NikonLd2.LensIDNumber', 'Exif.NikonLd3.LensIDNumber', 'Exif.Pentax.LensType'];
const SHUTTER_COUNT_KEYS = ['Exif.Nikon3.ShutterCount', 'Exif.Pentax.ShutterCount'];

let anyLensChecked = false;
let anyShutterChecked = false;

files.forEach((file) => {
  ex.getImageTags(path.resolve(file), (err, tags) => {
    console.log('===', file, '===');
    if (err) {
      console.log('  error:', err);
      return;
    }
    if (!tags) {
      console.log('  (no exif tags found)');
      return;
    }

    console.log('  Make/Model:', tags['Exif.Image.Make'], '/', tags['Exif.Image.Model']);

    LENS_ID_KEYS.forEach((key) => {
      if (key in tags) {
        anyLensChecked = true;
        const value = tags[key];
        const looksInterpreted = /[a-zA-Z]/.test(value);
        console.log(`  ${key} = ${JSON.stringify(value)}  [${looksInterpreted ? 'looks interpreted (contains letters)' : 'looks RAW (numeric only)'}]`);
      }
    });

    SHUTTER_COUNT_KEYS.forEach((key) => {
      if (key in tags) {
        anyShutterChecked = true;
        const value = tags[key];
        const asInt = parseInt(value, 10);
        const plausible = Number.isFinite(asInt) && asInt >= 0 && asInt < 5_000_000 && String(asInt) === String(value).trim();
        console.log(`  ${key} = ${JSON.stringify(value)}  [${plausible ? 'plausible plain integer' : 'NOT a plausible plain integer'}]`);
      }
    });
  });
});

process.on('exit', () => {
  if (!anyLensChecked) {
    console.log('\n(no manufacturer lens-ID field found in the given file(s) - use a Nikon/Pentax photo to exercise that part of the fix)');
  }
  if (!anyShutterChecked) {
    console.log('\n(no shutter-count field found in the given file(s))');
  }
});
