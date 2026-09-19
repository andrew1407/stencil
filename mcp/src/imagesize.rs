//! Pixel dimensions straight out of an image file's header — a port of the CLI's own
//! sniffer (`cli/src/scrape.zig`, `sniff`) and of the bot's `ImageDimensionReader`.
//!
//! The header carries the answer for PNG / GIF / BMP / JPEG / WebP; everything else
//! (video frames, URLs, exotic formats) falls back to a whole CLI render.

/// How much of the file the sniff reads. A JPEG's frame header sits past whatever EXIF and
/// thumbnail data precede it, so this is generous; the other formats need < 32 bytes.
const HEADER_BYTES: u64 = 64 * 1024;

const PNG_SIGNATURE: [u8; 8] = [0x89, b'P', b'N', b'G', 0x0D, 0x0A, 0x1A, 0x0A];

/// Read a local image file's dimensions, or `None` if it is not a readable local file in a
/// format this can measure. The bounded read runs off the async runtime.
pub async fn read_dimensions(input: &str) -> Option<(u32, u32)> {
    let path = std::path::PathBuf::from(input);
    if !path.is_file() {
        return None;
    }
    tokio::task::spawn_blocking(move || sniff(&read_header(&path)?))
        .await
        .ok()
        .flatten()
}

/// The first `HEADER_BYTES` of `path`.
fn read_header(path: &std::path::Path) -> Option<Vec<u8>> {
    use std::io::Read;
    let mut head = Vec::new();
    std::fs::File::open(path)
        .ok()?
        .take(HEADER_BYTES)
        .read_to_end(&mut head)
        .ok()?;
    Some(head)
}

/// Dimensions from an image header, or `None` when the bytes are not one of the five
/// formats (or are truncated before the field that carries them).
pub fn sniff(b: &[u8]) -> Option<(u32, u32)> {
    // PNG: the 8-byte signature, a 4-byte length, "IHDR", then big-endian width/height.
    if b.len() >= 24 && b[..8] == PNG_SIGNATURE && &b[12..16] == b"IHDR" {
        return checked(u32be(b, 16), u32be(b, 20));
    }
    // GIF87a/GIF89a: little-endian logical-screen width/height after the signature.
    if b.len() >= 10 && (&b[..6] == b"GIF87a" || &b[..6] == b"GIF89a") {
        return checked(u16le(b, 6), u16le(b, 8));
    }
    // BMP: a little-endian, possibly negative (top-down) size in the DIB header.
    if b.len() >= 26 && &b[..2] == b"BM" {
        return checked(
            (u32le(b, 18) as i32).unsigned_abs(),
            (u32le(b, 22) as i32).unsigned_abs(),
        );
    }
    if b.len() >= 4 && b[0] == 0xFF && b[1] == 0xD8 {
        return jpeg(b);
    }
    if b.len() >= 30 && &b[..4] == b"RIFF" && &b[8..12] == b"WEBP" {
        return webp(b);
    }
    None
}

/// JPEG: walk the marker segments to the first SOFn frame header.
fn jpeg(b: &[u8]) -> Option<(u32, u32)> {
    let mut pos = 2usize;
    while pos + 9 <= b.len() {
        if b[pos] != 0xFF {
            return None; // lost sync where a marker must start
        }
        let marker = b[pos + 1];
        if marker == 0xFF {
            pos += 1; // fill byte
            continue;
        }
        // Standalone markers (TEM, RSTn, SOI/EOI) carry no length field.
        if marker == 0x01 || (0xD0..=0xD9).contains(&marker) {
            pos += 2;
            continue;
        }
        // SOF0..SOF15 hold the frame size; C4/C8/CC are DHT/JPG/DAC, not frames.
        if (0xC0..=0xCF).contains(&marker) && !matches!(marker, 0xC4 | 0xC8 | 0xCC) {
            return checked(u16be(b, pos + 7), u16be(b, pos + 5));
        }
        let segment = u16be(b, pos + 2) as usize;
        if segment < 2 {
            return None;
        }
        pos += 2 + segment;
    }
    None
}

/// WebP: a RIFF container holding a VP8 (lossy), VP8L (lossless) or VP8X (extended) chunk.
fn webp(b: &[u8]) -> Option<(u32, u32)> {
    match &b[12..16] {
        // Lossy: a 3-byte frame tag, the 9D 01 2A start code, then 14-bit dimensions.
        b"VP8 " => checked(u16le(b, 26) & 0x3FFF, u16le(b, 28) & 0x3FFF),
        // Lossless: a 0x2F signature byte, then packed 14-bit (width-1)/(height-1).
        b"VP8L" if b[20] == 0x2F => {
            let bits = u32le(b, 21);
            checked((bits & 0x3FFF) + 1, ((bits >> 14) & 0x3FFF) + 1)
        }
        // Extended: 4 flag/reserved bytes, then 24-bit (canvas size - 1), little-endian.
        b"VP8X" => checked(u24le(b, 24) + 1, u24le(b, 27) + 1),
        _ => None,
    }
}

/// A pair is only an answer when both sides are non-zero.
fn checked(width: u32, height: u32) -> Option<(u32, u32)> {
    (width > 0 && height > 0).then_some((width, height))
}

fn u16be(b: &[u8], i: usize) -> u32 {
    u32::from(b[i]) << 8 | u32::from(b[i + 1])
}

fn u32be(b: &[u8], i: usize) -> u32 {
    u32::from_be_bytes([b[i], b[i + 1], b[i + 2], b[i + 3]])
}

fn u16le(b: &[u8], i: usize) -> u32 {
    u32::from(b[i]) | u32::from(b[i + 1]) << 8
}

fn u24le(b: &[u8], i: usize) -> u32 {
    u32::from(b[i]) | u32::from(b[i + 1]) << 8 | u32::from(b[i + 2]) << 16
}

fn u32le(b: &[u8], i: usize) -> u32 {
    u32::from_le_bytes([b[i], b[i + 1], b[i + 2], b[i + 3]])
}
