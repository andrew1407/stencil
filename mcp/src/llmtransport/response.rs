//! Response reading: the status line, the headers, and the body per its framing.

use std::io::Read;
use std::net::TcpStream;

use super::{LlmError, BODY_TOO_LARGE, MAX_BODY_BYTES, MAX_HEADER_BYTES};

/// Read one HTTP response off `stream`: status line + headers, then the body per its
/// framing (`Transfer-Encoding: chunked` > `Content-Length` > read-to-EOF).
pub(super) fn read_response(stream: &mut TcpStream) -> Result<(u16, String), LlmError> {
    let mut buf: Vec<u8> = Vec::with_capacity(8 * 1024);

    // 1. Accumulate until the header terminator, scanning only what each read brought in.
    let mut scanned = 0usize;
    let header_end = loop {
        if let Some(i) = find_subslice(&buf[scanned..], b"\r\n\r\n") {
            break scanned + i;
        }
        scanned = buf.len().saturating_sub(3); // keep what a split terminator straddles
        if buf.len() > MAX_HEADER_BYTES {
            return Err(LlmError::BadResponse("response headers exceed 64 KiB".to_string()));
        }
        if fill(stream, &mut buf)? == 0 {
            return Err(LlmError::BadResponse(
                "connection closed before the response headers completed".to_string(),
            ));
        }
    };

    // 2. Status line + the framing headers.
    let head = std::str::from_utf8(&buf[..header_end])
        .map_err(|_| LlmError::BadResponse("response headers are not UTF-8".to_string()))?;
    let mut lines = head.split("\r\n");
    let status_line = lines.next().unwrap_or("");
    let mut parts = status_line.split_whitespace();
    let proto = parts.next().unwrap_or("");
    if !proto.starts_with("HTTP/") {
        return Err(LlmError::BadResponse(format!(
            "not an HTTP status line: '{status_line}'"
        )));
    }
    let status: u16 = parts.next().and_then(|s| s.parse().ok()).ok_or_else(|| {
        LlmError::BadResponse(format!("no status code in '{status_line}'"))
    })?;

    let mut content_length: Option<usize> = None;
    let mut chunked = false;
    for line in lines {
        let Some((name, value)) = line.split_once(':') else {
            continue;
        };
        match name.trim().to_ascii_lowercase().as_str() {
            "content-length" => content_length = value.trim().parse().ok(),
            "transfer-encoding" => {
                chunked = value.trim().to_ascii_lowercase().contains("chunked");
            }
            _ => {}
        }
    }

    // 3. Body — whatever already arrived past the terminator, plus the rest of the stream.
    let rest = buf.split_off(header_end + 4);
    let body = if chunked {
        read_chunked(rest, stream)?
    } else if let Some(len) = content_length {
        read_exact_len(rest, stream, len)?
    } else {
        read_to_eof(rest, stream)?
    };
    Ok((status, String::from_utf8_lossy(&body).into_owned()))
}

/// Decode a `Transfer-Encoding: chunked` body: hex-size line, chunk bytes, CRLF, repeated
/// until the `0` chunk (trailers are ignored). `buf` starts with whatever body bytes were
/// read alongside the headers; more is pulled from `stream` on demand.
fn read_chunked(mut buf: Vec<u8>, stream: &mut TcpStream) -> Result<Vec<u8>, LlmError> {
    let mut body: Vec<u8> = Vec::new();
    let mut pos = 0usize;
    loop {
        // The chunk-size line.
        let line_end = loop {
            if let Some(i) = find_subslice(&buf[pos..], b"\r\n") {
                break pos + i;
            }
            if fill(stream, &mut buf)? == 0 {
                return Err(LlmError::BadResponse(
                    "connection closed inside a chunked body".to_string(),
                ));
            }
        };
        let size_text = std::str::from_utf8(&buf[pos..line_end])
            .map_err(|_| LlmError::BadResponse("chunk size line is not UTF-8".to_string()))?;
        // Chunk extensions (";…") are allowed and ignored.
        let size_token = size_text.split(';').next().unwrap_or("").trim();
        let size = usize::from_str_radix(size_token, 16).map_err(|_| {
            LlmError::BadResponse(format!("invalid chunk size '{size_token}'"))
        })?;
        pos = line_end + 2;

        if size == 0 {
            return Ok(body); // done; any trailers are ignored
        }
        if body.len() + size > MAX_BODY_BYTES {
            return Err(LlmError::BadResponse(BODY_TOO_LARGE.to_string()));
        }
        // The chunk data + its trailing CRLF.
        while buf.len() < pos + size + 2 {
            if fill(stream, &mut buf)? == 0 {
                return Err(LlmError::BadResponse(
                    "connection closed inside a chunked body".to_string(),
                ));
            }
        }
        body.extend_from_slice(&buf[pos..pos + size]);
        // Drop consumed bytes so `buf` never holds the raw stream alongside `body`.
        buf.drain(..pos + size + 2);
        pos = 0;
    }
}

/// Read until `buf` holds exactly `len` body bytes (a declared `Content-Length`).
fn read_exact_len(
    mut buf: Vec<u8>,
    stream: &mut TcpStream,
    len: usize,
) -> Result<Vec<u8>, LlmError> {
    if len > MAX_BODY_BYTES {
        return Err(LlmError::BadResponse(BODY_TOO_LARGE.to_string()));
    }
    if buf.len() < len {
        let remaining = len - buf.len();
        buf.reserve(remaining);
        let read = Read::take(&mut *stream, remaining as u64)
            .read_to_end(&mut buf)
            .map_err(|e| LlmError::Io(format!("could not read the response: {e}")))?;
        if read < remaining {
            return Err(LlmError::BadResponse(format!(
                "connection closed after {} of {len} body bytes",
                buf.len()
            )));
        }
    }
    buf.truncate(len);
    Ok(buf)
}

/// No framing declared: `Connection: close` semantics — the body is everything until EOF.
fn read_to_eof(mut buf: Vec<u8>, stream: &mut TcpStream) -> Result<Vec<u8>, LlmError> {
    loop {
        if buf.len() > MAX_BODY_BYTES {
            return Err(LlmError::BadResponse(BODY_TOO_LARGE.to_string()));
        }
        if fill(stream, &mut buf)? == 0 {
            return Ok(buf);
        }
    }
}

/// One `read` into a scratch block appended to `buf`; returns the byte count (0 = EOF).
fn fill(stream: &mut TcpStream, buf: &mut Vec<u8>) -> Result<usize, LlmError> {
    let mut block = [0u8; 4096];
    let n = stream
        .read(&mut block)
        .map_err(|e| LlmError::Io(format!("could not read the response: {e}")))?;
    buf.extend_from_slice(&block[..n]);
    Ok(n)
}

/// First index of `needle` in `haystack`.
fn find_subslice(haystack: &[u8], needle: &[u8]) -> Option<usize> {
    haystack
        .windows(needle.len())
        .position(|window| window == needle)
}
