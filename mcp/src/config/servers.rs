//! The collaboration servers a tool call may reach: the operator's `STENCIL_MCP_SERVERS`
//! allowlist of origins, each with its token from `STENCIL_MCP_SERVER_TOKENS`.

/// One allowlisted origin and the operator's token for it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ServerEntry {
    pub origin: String,
    pub token: Option<String>,
}

/// The allowlist; empty means no server or remote is reachable at all.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Servers {
    entries: Vec<ServerEntry>,
}

impl Servers {
    /// `list` is a comma list of origins; `tokens` a comma list of `origin=token` pairs. A
    /// bad entry is skipped with a warning; a token for an unlisted origin too.
    pub fn parse(list: Option<&str>, tokens: Option<&str>, warnings: &mut Vec<String>) -> Servers {
        let mut entries: Vec<ServerEntry> = Vec::new();
        for raw in list.unwrap_or_default().split(',').map(str::trim).filter(|s| !s.is_empty()) {
            match origin_of(raw) {
                Ok(origin) if !entries.iter().any(|e| e.origin == origin) => {
                    entries.push(ServerEntry { origin, token: None })
                }
                Ok(_) => {}
                Err(e) => warnings.push(format!("STENCIL_MCP_SERVERS: skipping '{raw}': {e}")),
            }
        }
        for pair in tokens.unwrap_or_default().split(',').map(str::trim).filter(|s| !s.is_empty()) {
            let Some((raw, token)) = pair.split_once('=') else {
                warnings.push("STENCIL_MCP_SERVER_TOKENS: an entry is not origin=token".into());
                continue;
            };
            let entry = origin_of(raw.trim())
                .ok()
                .and_then(|origin| entries.iter_mut().find(|e| e.origin == origin));
            match entry {
                Some(entry) if !token.trim().is_empty() => entry.token = Some(token.trim().into()),
                _ => warnings.push(format!(
                    "STENCIL_MCP_SERVER_TOKENS: '{}' is not an allowlisted origin",
                    raw.trim()
                )),
            }
        }
        Servers { entries }
    }

    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    pub fn origins(&self) -> Vec<&str> {
        self.entries.iter().map(|e| e.origin.as_str()).collect()
    }

    /// The `STENCIL_SERVER_TOKENS` value a CLI child needs for `entries`: one `origin=token`
    /// pair per entry that has a token, so each URL the run dials finds its own.
    pub fn child_tokens<'a>(entries: impl IntoIterator<Item = &'a ServerEntry>) -> Option<String> {
        let mut pairs: Vec<String> = Vec::new();
        for entry in entries {
            if let Some(token) = &entry.token {
                let pair = format!("{}={token}", entry.origin);
                if !pairs.contains(&pair) {
                    pairs.push(pair);
                }
            }
        }
        (!pairs.is_empty()).then(|| pairs.join(","))
    }

    /// The allowlisted entry a caller-named URL points at, or why it is refused.
    pub fn find(&self, field: &str, url: &str) -> Result<&ServerEntry, String> {
        if self.entries.is_empty() {
            return Err(format!(
                "`{field}` is refused: this MCP server reaches collaboration servers only from \
                 the operator's allowlist (STENCIL_MCP_SERVERS), and none is configured"
            ));
        }
        let origin = origin_of(url).map_err(|e| format!("`{field}` '{url}': {e}"))?;
        self.entries.iter().find(|e| e.origin == origin).ok_or_else(|| {
            format!(
                "`{field}` '{origin}' is not an allowed server — the operator allows: {}",
                self.origins().join(", ")
            )
        })
    }
}

/// `scheme://host[:port]` with the host lowercased and a default port dropped. A bare host
/// is https unless it is loopback, as the CLI normalizes it (`cli/src/server/urls.zig`).
pub fn origin_of(url: &str) -> Result<String, String> {
    let url = url.trim();
    let (scheme, rest) = match url.split_once("://") {
        Some((scheme, rest)) => (scheme.to_ascii_lowercase(), rest),
        None => (String::new(), url),
    };
    let authority = rest.split(['/', '?', '#']).next().unwrap_or_default();
    if authority.contains('@') {
        return Err("a server URL may not carry credentials".into());
    }
    let (host, port) = match authority.strip_prefix('[') {
        Some(v6) => {
            let (host, after) = v6.split_once(']').ok_or("an unclosed IPv6 bracket")?;
            (format!("[{}]", host.to_ascii_lowercase()), after.strip_prefix(':'))
        }
        None => match authority.rsplit_once(':') {
            Some((host, port)) => (host.to_ascii_lowercase(), Some(port)),
            None => (authority.to_ascii_lowercase(), None),
        },
    };
    if host.is_empty() || host == "[]" {
        return Err("no host".into());
    }
    let scheme = match scheme.as_str() {
        "" if is_loopback(&host) => "http",
        "" | "https" => "https",
        "http" => "http",
        other => return Err(format!("'{other}://' is not http or https")),
    };
    let port = match port {
        None => None,
        Some(p) => Some(p.parse::<u16>().map_err(|_| format!("bad port '{p}'"))?),
    };
    let default = if scheme == "https" { 443 } else { 80 };
    Ok(match port.filter(|p| *p != default) {
        Some(port) => format!("{scheme}://{host}:{port}"),
        None => format!("{scheme}://{host}"),
    })
}

fn is_loopback(host: &str) -> bool {
    host == "localhost" || host == "[::1]" || host.starts_with("127.")
}
