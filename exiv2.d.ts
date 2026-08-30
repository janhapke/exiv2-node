// Type definitions for @janhapke/exiv2
// Hand-written from the native binding's actual behaviour (exiv2node.cc's
// InitAll()/argument validation/OnOK() callback shapes) and the JS-level
// helper layered on top in exiv2.js. Not derived from the README alone.

/**
 * A flat map of Exiv2 tag keys (e.g. "Exif.Image.DateTime",
 * "Iptc.Application2.RecordVersion", "Xmp.dc.subject") to their
 * interpreted string value.
 */
export interface Tags {
  [tagKey: string]: string;
}

/**
 * One embedded preview image found in a file's metadata.
 */
export interface PreviewImage {
  mimeType: string;
  height: number;
  width: number;
  data: Buffer;
}

/**
 * Tags to write, keyed by their Exiv2 tag key. Values are coerced to
 * strings on the native side: numbers are formatted (integers without a
 * decimal point), and anything else is coerced via its own `toString()`.
 * `string`/`number` are the two intended, well-defined input types.
 */
export type TagsToSet = Record<string, string | number>;

/**
 * Callback shape used by `setImageTags()` and `deleteImageTags()`.
 * `err` is the raw exception message string (not an `Error` instance) if
 * something went wrong, or `null` on success.
 */
export type ErrorCallback = (err: string | null) => void;

/**
 * Callback shape used by `getImageTags()`.
 * `err` is the raw exception message string (not an `Error` instance) if
 * something went wrong, or `null` on success. `tags` is `null` when the
 * file has no readable EXIF/IPTC/XMP tags at all, or when `err` is set;
 * otherwise it's the tag map.
 */
export type TagsCallback = (err: string | null, tags: Tags | null) => void;

/**
 * Callback shape used by `getImagePreviews()`.
 * `err` is the raw exception message string (not an `Error` instance) if
 * something went wrong, or `null` on success. `previews` is `null` only
 * when `err` is set; on success it is always an array (possibly empty).
 */
export type PreviewsCallback = (err: string | null, previews: PreviewImage[] | null) => void;

/**
 * Read all EXIF/IPTC/XMP tags from an image file or in-memory buffer.
 * Runs off the main thread; the callback fires once decoding finishes.
 *
 * @throws {TypeError} synchronously if `callback` is missing/not a
 *   function, or if `input` is neither a string nor a `Buffer`.
 */
export function getImageTags(input: string | Buffer, callback: TagsCallback): void;

/**
 * Write (or overwrite) the given tags on an image file or in-memory
 * buffer, then re-encode the metadata back into the file.
 *
 * Note: when `input` is a `Buffer`, the buffer itself is read as the
 * source image but the native binding has no way to hand back a modified
 * buffer — there is no rewritten-bytes output. `callback` only reports
 * success/failure.
 *
 * @throws {TypeError} synchronously if `callback` is missing/not a
 *   function, if `tags` is not a plain object (an array is rejected too),
 *   or if `input` is neither a string nor a `Buffer`.
 */
export function setImageTags(input: string | Buffer, tags: TagsToSet, callback: ErrorCallback): void;

/**
 * Delete the named tags from an image file or in-memory buffer, then
 * re-encode the metadata back into the file. Tag names that don't exist
 * on the file are silently ignored.
 *
 * Note: same buffer caveat as `setImageTags()` — no rewritten bytes are
 * returned for buffer input.
 *
 * @throws {TypeError} synchronously if `callback` is missing/not a
 *   function, if `tagNames` is not an array, or if `input` is neither a
 *   string nor a `Buffer`.
 */
export function deleteImageTags(input: string | Buffer, tagNames: string[], callback: ErrorCallback): void;

/**
 * Extract the embedded preview images (e.g. a RAW file's JPEG preview)
 * from an image file or in-memory buffer.
 *
 * @throws {TypeError} synchronously if `callback` is missing/not a
 *   function, or if `input` is neither a string nor a `Buffer`.
 */
export function getImagePreviews(input: string | Buffer, callback: PreviewsCallback): void;

/**
 * JS-level helper (not part of the native binding): converts an Exiv2
 * date-time tag string (e.g. "2012:04:14 17:45:52") from a tag map
 * previously returned by `getImageTags()` into a `Date`.
 *
 * @param tags A tag map, as returned by `getImageTags()`.
 * @param dateTimeTag Tag to read the date/time from. Defaults to
 *   `'Exif.Photo.DateTimeOriginal'`. If a matching sub-second tag exists
 *   (found by replacing `.DateTime` with `.SubSecTime` in this tag's
 *   name, e.g. `'Exif.Photo.SubSecTimeOriginal'`), its value is applied
 *   as milliseconds (scaled from hundredths of a second).
 * @returns The parsed `Date`, or `null` if `dateTimeTag` isn't present
 *   in `tags`.
 */
export function getDate(tags: Tags, dateTimeTag?: string): Date | null;
