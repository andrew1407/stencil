//! Header-sniffed pixel dimensions (`stencil_probe`'s fast path).
//!
//! The sniff stands in for a whole CLI render, so it must agree with what the CLI reports
//! and must decline whatever it cannot measure. The CLI's own sniffer
//! (`cli/src/scrape.zig`) is the reference these headers are written against.

use stencil_mcp::imagesize::{read_dimensions, sniff};

/// A minimal PNG header: signature, the IHDR length + tag, then big-endian width/height.
fn png(width: u32, height: u32) -> Vec<u8> {
    let mut b = vec![0x89, b'P', b'N', b'G', 0x0D, 0x0A, 0x1A, 0x0A];
    b.extend_from_slice(&13u32.to_be_bytes());
    b.extend_from_slice(b"IHDR");
    b.extend_from_slice(&width.to_be_bytes());
    b.extend_from_slice(&height.to_be_bytes());
    b
}

#[test]
fn png_ihdr_dimensions() {
    assert_eq!(sniff(&png(16, 12)), Some((16, 12)));
}

#[test]
fn gif_logical_screen_dimensions() {
    let mut b = b"GIF89a".to_vec();
    b.extend_from_slice(&[0x40, 0x01, 0xF0, 0x00]); // 320 x 240, little-endian
    assert_eq!(sniff(&b), Some((320, 240)));
}

/// BMP stores a top-down image as a negative height; the CLI reports the magnitude.
#[test]
fn bmp_top_down_height_is_reported_positive() {
    let mut b = vec![0u8; 26];
    b[0] = b'B';
    b[1] = b'M';
    b[18..22].copy_from_slice(&8i32.to_le_bytes());
    b[22..26].copy_from_slice(&(-6i32).to_le_bytes());
    assert_eq!(sniff(&b), Some((8, 6)));
}

/// JPEG: the frame header sits past segments of arbitrary length, so the walk has to skip
/// them by their own size — and the SOF carries HEIGHT first.
#[test]
fn jpeg_walks_segments_to_the_frame_header() {
    let mut b = vec![0xFF, 0xD8];
    b.extend_from_slice(&[0xFF, 0xE0, 0x00, 0x06, 1, 2, 3, 4]); // APP0, 4 payload bytes
    b.extend_from_slice(&[0xFF, 0xC0, 0x00, 0x11, 0x08]); // SOF0 + precision
    b.extend_from_slice(&[0x00, 0x64, 0x01, 0x2C]); // height 100, width 300
    b.extend_from_slice(&[0u8; 8]);
    assert_eq!(sniff(&b), Some((300, 100)));
}

/// A restart/standalone marker carries no length field — treating one as a segment would
/// walk off into the entropy-coded data and miss the frame entirely.
#[test]
fn jpeg_skips_standalone_markers() {
    let mut b = vec![0xFF, 0xD8, 0xFF, 0xD0, 0xFF, 0xFF];
    b.extend_from_slice(&[0xFF, 0xC2, 0x00, 0x11, 0x08, 0x00, 0x0C, 0x00, 0x10]); // SOF2 12x16
    b.extend_from_slice(&[0u8; 8]);
    assert_eq!(sniff(&b), Some((16, 12)));
}

#[test]
fn webp_lossy_lossless_and_extended_all_measure() {
    let mut lossy = vec![0u8; 30];
    lossy[..4].copy_from_slice(b"RIFF");
    lossy[8..12].copy_from_slice(b"WEBP");
    lossy[12..16].copy_from_slice(b"VP8 ");
    lossy[23..26].copy_from_slice(&[0x9D, 0x01, 0x2A]);
    lossy[26..28].copy_from_slice(&64u16.to_le_bytes());
    lossy[28..30].copy_from_slice(&48u16.to_le_bytes());
    assert_eq!(sniff(&lossy), Some((64, 48)));

    // Lossless packs (width-1) and (height-1) as two 14-bit fields after the 0x2F byte.
    let mut lossless = lossy.clone();
    lossless[12..16].copy_from_slice(b"VP8L");
    lossless[20] = 0x2F;
    let bits: u32 = (63) | (47 << 14);
    lossless[21..25].copy_from_slice(&bits.to_le_bytes());
    assert_eq!(sniff(&lossless), Some((64, 48)));

    // Extended: 24-bit (canvas size - 1) after four flag/reserved bytes.
    let mut extended = lossy.clone();
    extended[12..16].copy_from_slice(b"VP8X");
    extended[24..27].copy_from_slice(&[0x3F, 0x00, 0x00]);
    extended[27..30].copy_from_slice(&[0x2F, 0x00, 0x00]);
    assert_eq!(sniff(&extended), Some((64, 48)));
}

/// Anything else — a video container, a truncated header, a zero dimension — must decline
/// so `run_probe` falls back to the CLI rather than reporting a made-up size.
#[test]
fn unmeasurable_bytes_decline() {
    for bytes in [
        b"\x00\x00\x00\x18ftypmp42".to_vec(), // an MP4 box
        b"<svg width='10'></svg>".to_vec(),
        png(16, 12)[..20].to_vec(), // truncated before the height
        png(0, 12),                 // a zero dimension is not an answer
        Vec::new(),
    ] {
        assert_eq!(sniff(&bytes), None, "should decline: {bytes:?}");
    }
}

/// The file-reading wrapper: a real PNG on disk answers, a missing path and a directory
/// decline (they are the CLI's problem, not a sniff failure).
#[tokio::test]
async fn read_dimensions_reads_a_file_and_declines_anything_else() {
    let dir = tempfile::tempdir().expect("temp dir");
    let file = dir.path().join("sample.png");
    std::fs::write(&file, png(24, 9)).expect("write the fixture");

    assert_eq!(read_dimensions(&file.to_string_lossy()).await, Some((24, 9)));
    assert_eq!(read_dimensions(&dir.path().to_string_lossy()).await, None);
    assert_eq!(read_dimensions("no/such/file.png").await, None);
    assert_eq!(read_dimensions("https://example.com/a.png").await, None);
}

/// The real fixture the CLI's own suite uses: the sniff must report exactly what the CLI
/// reports for it (16x12), since it now answers `stencil_probe` in its place.
#[tokio::test]
async fn the_shared_cli_fixture_measures_as_the_cli_reports_it() {
    let fixture = concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../cli/tests/fixtures/sample.png"
    );
    assert_eq!(read_dimensions(fixture).await, Some((16, 12)));
}
