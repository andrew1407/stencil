//! Reading a CLI child's pipes without letting them grow without bound: stderr keeps its
//! last [`STDERR_TAIL`] bytes as whole lines, stdout is read up to [`STDOUT_CAP`].

use std::collections::VecDeque;

use tokio::io::{AsyncRead, AsyncReadExt};

pub const STDERR_TAIL: usize = 1024 * 1024;
pub const STDOUT_CAP: usize = 16 * 1024 * 1024;

/// A line longer than this is cut where it stands, so one runaway line cannot pin memory.
const LINE_CAP: usize = 64 * 1024;

/// The last `limit` bytes of a stream as whole lines, oldest dropped first.
pub struct Tail {
    lines: VecDeque<String>,
    bytes: usize,
    limit: usize,
}

impl Tail {
    pub fn new(limit: usize) -> Tail {
        Tail { lines: VecDeque::new(), bytes: 0, limit }
    }

    pub fn push(&mut self, line: String) {
        self.bytes += line.len() + 1;
        self.lines.push_back(line);
        while self.bytes > self.limit && self.lines.len() > 1 {
            let dropped = self.lines.pop_front().unwrap_or_default();
            self.bytes -= dropped.len() + 1;
        }
    }

    pub fn into_text(self) -> String {
        let mut text = String::with_capacity(self.bytes);
        for line in self.lines {
            text.push_str(&line);
            text.push('\n');
        }
        text
    }
}

/// Read `pipe` to its end one line at a time into a [`Tail`] of `limit` bytes, handing every
/// line to `on_line` as it arrives.
pub async fn read_lines<R: AsyncRead + Unpin>(
    mut pipe: R,
    limit: usize,
    mut on_line: impl FnMut(&str),
) -> String {
    let mut tail = Tail::new(limit);
    let mut emit = |bytes: &[u8]| {
        let text = String::from_utf8_lossy(bytes);
        let text = text.strip_suffix('\r').unwrap_or(&text);
        on_line(text);
        tail.push(text.to_string());
    };
    let mut pending: Vec<u8> = Vec::new();
    let mut block = [0u8; 8192];
    loop {
        let n = match pipe.read(&mut block).await {
            Ok(0) | Err(_) => break,
            Ok(n) => n,
        };
        pending.extend_from_slice(&block[..n]);
        while let Some(end) = pending.iter().position(|&b| b == b'\n') {
            emit(&pending[..end]);
            pending.drain(..=end);
        }
        if pending.len() > LINE_CAP {
            emit(&pending);
            pending.clear();
        }
    }
    if !pending.is_empty() {
        emit(&pending);
    }
    tail.into_text()
}

/// Read `pipe` whole, refusing past `cap` bytes.
pub async fn read_capped<R: AsyncRead + Unpin>(pipe: R, cap: usize) -> Result<String, String> {
    let mut bytes = Vec::new();
    let mut limited = pipe.take(cap as u64 + 1);
    if let Err(e) = limited.read_to_end(&mut bytes).await {
        return Err(format!("could not read the stencil CLI's output: {e}"));
    }
    if bytes.len() > cap {
        return Err(format!("the stencil CLI printed more than {} MiB on stdout", cap >> 20));
    }
    Ok(String::from_utf8_lossy(&bytes).into_owned())
}
