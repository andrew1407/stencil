//! The LLM client: provider configuration, the canonical system prompt, and the three
//! provider wire mappings from `llm-contract.md` (the authoritative contract this
//! module ports — §4 prompt, §5 configuration, §6 wire mappings).
//!
//! Providers: `ollama` (native `/api/chat`), `openai-compat` (LM Studio & friends,
//! `/chat/completions`), and `stencil-server` (a Stencil collaboration server proxying
//! Anthropic at `/llm/chat`). All requests go through the [`crate::llmtransport`] trait so
//! tests can substitute a recording mock; the real transport is plain-http only.
//!
//! Like the other clients, this module never touches pixels: it returns the model's raw
//! reply text, which `opplan` parses into a strictly validated plan.

use serde::Serialize;
use serde_json::Value;

use crate::config::LlmEnv;
use crate::llmtransport::{clip, LlmError, LlmTransport, SNIPPET_LEN};
use crate::registry;

/// The §4 prose core around the generated ops section — `head` ends at the "Available
/// ops" heading, `tail` follows the bullets — loaded verbatim from the canonical
/// cross-surface asset (see `browser/js/config/llm/README.md`), embedded at compile
/// time. The bullets themselves live in [`crate::registry::op_registry`] (§13).
static PROMPT_PROSE: std::sync::LazyLock<(String, String)> = std::sync::LazyLock::new(|| {
    let asset: Value =
        serde_json::from_str(include_str!("../../browser/js/config/llm/systemPrompt.json"))
            .expect("canonical browser/js/config/llm/systemPrompt.json is not valid JSON");
    let field = |key: &str| {
        asset[key]
            .as_str()
            .unwrap_or_else(|| panic!("systemPrompt.json: \"{key}\" must be a string"))
            .to_owned()
    };
    (field("head"), field("tail"))
});

/// The canonical system prompt — contract §4: the verbatim prose core around an ops
/// section GENERATED from the op registry (§13), assembled once at first use. A registry
/// mistake (forbidden name, censor-matching bullet) panics here — loudly, at assembly —
/// rather than leaking into the prompt.
pub fn llm_system_prompt() -> &'static str {
    static PROMPT: std::sync::OnceLock<String> = std::sync::OnceLock::new();
    PROMPT.get_or_init(|| {
        let ops =
            crate::registry::assemble_ops_section(registry::op_registry(), registry::WIRED_CAPABILITIES)
                .expect("§4 ops-section assembly from the op registry");
        let (head, tail) = &*PROMPT_PROSE;
        format!("{head}{ops}{tail}")
    })
}

/// Attachments larger than this are sent text-only with a note (this adapter is codec-free,
/// so it cannot downscale; the cap keeps payloads sane).
pub const MAX_IMAGE_BYTES: u64 = 8 * 1024 * 1024;

/// Contract §7: appended to the system-prompt suffix when — and only when — the edge map
/// is actually attached. Verbatim.
pub const EDGE_MAP_SUFFIX: &str = "The second attached image is an edge-map render of the \
working image at the same pixel coordinates: use it to place outline points on real edges.";

// ── Providers & configuration (contract §5) ──

/// The `providers` table of the canonical cross-surface asset, embedded at compile time.
static PROVIDERS: std::sync::LazyLock<Value> = std::sync::LazyLock::new(|| {
    let asset: Value =
        serde_json::from_str(include_str!("../../browser/js/config/llm/providers.json"))
            .expect("canonical browser/js/config/llm/providers.json is not valid JSON");
    asset["providers"].clone()
});

/// The three providers every Stencil client supports.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Provider {
    /// Native Ollama chat (`POST {baseUrl}/api/chat`).
    Ollama,
    /// OpenAI-compatible servers — LM Studio, llama.cpp, vLLM … (`POST {baseUrl}/chat/completions`).
    OpenaiCompat,
    /// A Stencil collaboration server proxying Anthropic (`POST {serverUrl}/llm/chat`).
    StencilServer,
}

impl Provider {
    /// Parse a provider token (trimmed, case-insensitive).
    pub fn parse(token: &str) -> Result<Provider, String> {
        match token.trim().to_ascii_lowercase().as_str() {
            "ollama" => Ok(Provider::Ollama),
            "openai-compat" => Ok(Provider::OpenaiCompat),
            "stencil-server" => Ok(Provider::StencilServer),
            other => Err(format!(
                "unknown LLM provider '{other}' (expected: ollama, openai-compat, stencil-server)"
            )),
        }
    }

    pub fn as_str(self) -> &'static str {
        match self {
            Provider::Ollama => "ollama",
            Provider::OpenaiCompat => "openai-compat",
            Provider::StencilServer => "stencil-server",
        }
    }

    /// The contract's per-provider default `baseUrl` (§5), from the embedded canonical
    /// providers.json. `stencil-server` carries `null` there — its endpoint is the
    /// separately-configured server URL.
    pub fn default_base_url(self) -> Option<&'static str> {
        PROVIDERS[self.as_str()]["defaultBaseUrl"].as_str()
    }
}

/// Resolved provider configuration for one call: the `STENCIL_LLM_*` environment (already
/// layered with `.env` by `config.rs`) plus the per-call tool overrides.
#[derive(Debug, Clone)]
pub struct LlmConfig {
    pub provider: Provider,
    /// For `ollama`/`openai-compat`; trailing slash trimmed. Unused for `stencil-server`.
    pub base_url: String,
    /// May be empty (provider/server default).
    pub model: String,
    /// `openai-compat` only; empty = no `Authorization` header.
    pub api_key: String,
    /// `stencil-server` only.
    pub server_url: String,
    /// `stencil-server` only; empty = no `Authorization` header.
    pub server_token: String,
}

impl LlmConfig {
    /// Resolve the provider config from the environment, filling the provider defaults.
    /// `model` is the ONLY per-call override: the key travels with the endpoint, so a
    /// caller-chosen host would exfiltrate it. Errors are ready-made user-facing messages.
    pub fn resolve(env: &LlmEnv, model: Option<&str>) -> Result<LlmConfig, String> {
        let token = env.provider.as_deref().unwrap_or("ollama");
        let provider = Provider::parse(token)?;

        let base_url = env
            .base_url
            .as_deref()
            .or(provider.default_base_url())
            .unwrap_or_default();

        let server_url = env.server_url.as_deref().unwrap_or_default();
        if provider == Provider::StencilServer && server_url.trim().is_empty() {
            return Err(
                "the stencil-server provider needs STENCIL_LLM_SERVER_URL set to a Stencil \
                 collaboration server URL"
                    .to_string(),
            );
        }

        Ok(LlmConfig {
            provider,
            base_url: base_url.trim().trim_end_matches('/').to_string(),
            model: model.or(env.model.as_deref()).unwrap_or_default().to_string(),
            api_key: env.api_key.clone().unwrap_or_default(),
            server_url: server_url.trim().trim_end_matches('/').to_string(),
            server_token: env.server_token.clone().unwrap_or_default(),
        })
    }
}

// ── Messages ──

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Role {
    User,
    Assistant,
}

impl Role {
    pub fn as_str(self) -> &'static str {
        match self {
            Role::User => "user",
            Role::Assistant => "assistant",
        }
    }
}

/// One image attached to a message: its media type and **already-base64** data.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ImageAttachment {
    /// `image/png`, `image/jpeg`, `image/webp`, or `image/gif` (contract §7).
    pub media_type: String,
    /// Base64 of the raw bytes (no data-URL prefix; mappings add their own framing).
    pub data: String,
}

/// One chat message. History (when a caller keeps any) is replayed in full per contract §7;
/// the `stencil_prompt` tool keeps none, so every round sends exactly one user message
/// (the §7 auto-continuation re-sends the turn once, still as a single message).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ChatMessage {
    pub role: Role,
    pub text: String,
    pub images: Vec<ImageAttachment>,
}

// ── Errors ──

/// Everything `chat` can fail with. Hand-written, `Display` is the user-facing message.
#[derive(Debug)]
pub enum ChatError {
    /// The HTTP layer failed (scheme/connect/read/status).
    Transport(LlmError),
    /// stencil-server `stopReason: "max_tokens"` — the reply is truncated and per contract
    /// must NOT be parsed as a plan.
    Truncated,
    /// stencil-server `stopReason: "refusal"` — shown as an error, never parsed as a plan.
    Refusal(String),
    /// The provider answered 2xx but not in the documented response shape.
    BadReply(String),
    /// The server's 503 llmDisabled: no LLM key configured — a configure hint, not a
    /// broken transport. Carries the server's own (sanitized) message.
    Disabled(String),
}

impl std::fmt::Display for ChatError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ChatError::Transport(e) => e.fmt(f),
            ChatError::Truncated => f.write_str(
                "the LLM response was truncated (stopReason \"max_tokens\") and was not \
                 parsed as a plan — retry with a shorter request, or raise the server's \
                 LLM_MAX_TOKENS",
            ),
            ChatError::Refusal(text) => {
                if text.trim().is_empty() {
                    f.write_str("the LLM refused to answer (stopReason \"refusal\")")
                } else {
                    write!(f, "the LLM refused to answer: {}", text.trim())
                }
            }
            ChatError::BadReply(detail) => write!(f, "unexpected LLM response: {detail}"),
            // The reason once (§6.3): the server's own message when it has one.
            ChatError::Disabled(reason) if reason.is_empty() => {
                f.write_str("LLM support is disabled on this server (no API key configured)")
            }
            ChatError::Disabled(reason) => f.write_str(reason),
        }
    }
}

impl std::error::Error for ChatError {}

// ── Chat ──

/// Send one chat completion: build the provider request (§6), POST it through `transport`,
/// and extract the reply text. stencil-server stop reasons map to typed errors.
/// `system_suffix` is the §4 dynamic suffix, appended after the canonical prompt (empty =
/// the prompt rides alone).
pub fn chat(
    transport: &dyn LlmTransport,
    config: &LlmConfig,
    messages: &[ChatMessage],
    system_suffix: &str,
) -> Result<String, ChatError> {
    let (url, headers, body) = build_request(config, messages, system_suffix);
    let response = transport.post_json(&url, &headers, &body).map_err(|e| match e {
        // Keyed on the body's code like every client (browser 'disabled' parity).
        LlmError::Status { reason, code, .. } if code == "llmDisabled" => {
            ChatError::Disabled(reason)
        }
        other => ChatError::Transport(other),
    })?;
    extract_reply(config.provider, &response)
}

// The provider wire shapes (§6), as structs of borrows so building a request never copies
// message text or base64 image data — the only copy is the final serialization.

/// §6.1/§6.2 request body: `{"model", "stream": false, "messages"}`.
#[derive(Serialize)]
struct ChatBody<'a, M: Serialize> {
    model: &'a str,
    stream: bool,
    messages: Vec<M>,
}

/// §6.1 message: images ride as bare base64 strings.
#[derive(Serialize)]
struct OllamaMessage<'a> {
    role: &'a str,
    content: &'a str,
    #[serde(skip_serializing_if = "Option::is_none")]
    images: Option<Vec<&'a str>>,
}

/// §6.2 message: `content` is a plain string, or text + image-URL parts.
#[derive(Serialize)]
struct OpenaiMessage<'a> {
    role: &'a str,
    content: OpenaiContent<'a>,
}

#[derive(Serialize)]
#[serde(untagged)]
enum OpenaiContent<'a> {
    Text(&'a str),
    Parts(Vec<OpenaiPart<'a>>),
}

#[derive(Serialize)]
#[serde(tag = "type")]
enum OpenaiPart<'a> {
    #[serde(rename = "text")]
    Text { text: &'a str },
    #[serde(rename = "image_url")]
    ImageUrl { image_url: DataUrl },
}

#[derive(Serialize)]
struct DataUrl {
    url: String,
}

/// §6.3 request body: `protocol.LlmChatRequest` (`model` omitted when empty).
#[derive(Serialize)]
struct ServerBody<'a> {
    system: &'a str,
    messages: Vec<ServerMessage<'a>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    model: Option<&'a str>,
}

#[derive(Serialize)]
struct ServerMessage<'a> {
    role: &'a str,
    text: &'a str,
    #[serde(skip_serializing_if = "Option::is_none")]
    images: Option<Vec<ServerImage<'a>>>,
}

#[derive(Serialize)]
struct ServerImage<'a> {
    #[serde(rename = "mediaType")]
    media_type: &'a str,
    data: &'a str,
}

/// `Authorization: Bearer <token>` when `token` is non-empty; no extra headers otherwise.
fn bearer(token: &str) -> Vec<(String, String)> {
    if token.is_empty() {
        Vec::new()
    } else {
        vec![("Authorization".to_string(), format!("Bearer {token}"))]
    }
}

fn to_json(body: &impl Serialize) -> String {
    serde_json::to_string(body).expect("the wire body serializes")
}

/// Map the messages onto the provider's wire shape: `(url, extra headers, JSON body)`.
fn build_request(
    config: &LlmConfig,
    messages: &[ChatMessage],
    system_suffix: &str,
) -> (String, Vec<(String, String)>, String) {
    // §4: the canonical prompt verbatim, then the dynamic suffix (nothing is prepended).
    let base = llm_system_prompt();
    let system: std::borrow::Cow<'_, str> = if system_suffix.is_empty() {
        std::borrow::Cow::Borrowed(base)
    } else {
        std::borrow::Cow::Owned(format!("{base}\n\n{system_suffix}"))
    };
    match config.provider {
        // §6.1 — native Ollama chat.
        Provider::Ollama => {
            let mut wire = vec![OllamaMessage {
                role: "system",
                content: &system,
                images: None,
            }];
            wire.extend(messages.iter().map(|m| OllamaMessage {
                role: m.role.as_str(),
                content: &m.text,
                images: (!m.images.is_empty())
                    .then(|| m.images.iter().map(|i| i.data.as_str()).collect()),
            }));
            let body = ChatBody {
                model: &config.model,
                stream: false,
                messages: wire,
            };
            (
                format!("{}/api/chat", config.base_url),
                Vec::new(),
                to_json(&body),
            )
        }

        // §6.2 — OpenAI-compatible: images as data-URL content parts; optional bearer key.
        Provider::OpenaiCompat => {
            let mut wire = vec![OpenaiMessage {
                role: "system",
                content: OpenaiContent::Text(&system),
            }];
            wire.extend(messages.iter().map(|m| OpenaiMessage {
                role: m.role.as_str(),
                content: if m.images.is_empty() {
                    OpenaiContent::Text(&m.text)
                } else {
                    let mut parts = vec![OpenaiPart::Text { text: &m.text }];
                    parts.extend(m.images.iter().map(|i| OpenaiPart::ImageUrl {
                        image_url: DataUrl {
                            url: format!("data:{};base64,{}", i.media_type, i.data),
                        },
                    }));
                    OpenaiContent::Parts(parts)
                },
            }));
            let body = ChatBody {
                model: &config.model,
                stream: false,
                messages: wire,
            };
            (
                format!("{}/chat/completions", config.base_url),
                bearer(&config.api_key),
                to_json(&body),
            )
        }

        // §6.3 — the collaboration server's Anthropic proxy: protocol.LlmChatRequest.
        Provider::StencilServer => {
            let body = ServerBody {
                system: &system,
                messages: messages
                    .iter()
                    .map(|m| ServerMessage {
                        role: m.role.as_str(),
                        text: &m.text,
                        images: (!m.images.is_empty()).then(|| {
                            m.images
                                .iter()
                                .map(|i| ServerImage {
                                    media_type: &i.media_type,
                                    data: &i.data,
                                })
                                .collect()
                        }),
                    })
                    .collect(),
                model: (!config.model.is_empty()).then_some(config.model.as_str()),
            };
            (
                format!("{}/llm/chat", config.server_url),
                bearer(&config.server_token),
                to_json(&body),
            )
        }
    }
}

/// Pull the reply text out of a provider's 2xx response body.
fn extract_reply(provider: Provider, body: &str) -> Result<String, ChatError> {
    let value: Value = serde_json::from_str(body).map_err(|e| {
        ChatError::BadReply(format!(
            "the provider returned non-JSON ({e}): {}",
            clip(body, SNIPPET_LEN)
        ))
    })?;
    match provider {
        // Reply text = `message.content`.
        Provider::Ollama => value["message"]["content"]
            .as_str()
            .map(str::to_string)
            .ok_or_else(|| {
                ChatError::BadReply(format!(
                    "no message.content string in the ollama response: {}",
                    clip(body, SNIPPET_LEN)
                ))
            }),
        // Reply text = `choices[0].message.content`.
        Provider::OpenaiCompat => value["choices"][0]["message"]["content"]
            .as_str()
            .map(str::to_string)
            .ok_or_else(|| {
                ChatError::BadReply(format!(
                    "no choices[0].message.content string in the response: {}",
                    clip(body, SNIPPET_LEN)
                ))
            }),
        // protocol.LlmChatResponse: `text` + `stopReason` (max_tokens/refusal are typed).
        Provider::StencilServer => {
            let text = value["text"].as_str();
            match value["stopReason"].as_str().unwrap_or("end_turn") {
                "max_tokens" => Err(ChatError::Truncated),
                "refusal" => Err(ChatError::Refusal(text.unwrap_or_default().to_string())),
                _ => text.map(str::to_string).ok_or_else(|| {
                    ChatError::BadReply(format!(
                        "no text string in the stencil-server response: {}",
                        clip(body, SNIPPET_LEN)
                    ))
                }),
            }
        }
    }
}

// ── Attachments ──

/// Try to read a local image file into a vision attachment (contract §7 media types).
/// Returns `(attachment, note)`:
/// - a URL / non-existent path / directory → `(None, None)` — it's a CLI-side source
///   (web URL or server project name), silently sent text-only;
/// - an existing file with an unsupported extension, over the 8 MiB cap, or unreadable →
///   `(None, Some(note))` — sent text-only with a human-readable note;
/// - otherwise `(Some(attachment), None)`.
pub fn attach_local_image(input: &str) -> (Option<ImageAttachment>, Option<String>) {
    let path = std::path::Path::new(input);
    if !path.is_file() {
        return (None, None);
    }

    let extension = path
        .extension()
        .map(|e| e.to_string_lossy().to_ascii_lowercase())
        .unwrap_or_default();
    let media_type = match extension.as_str() {
        "png" => "image/png",
        "jpg" | "jpeg" => "image/jpeg",
        "webp" => "image/webp",
        "gif" => "image/gif",
        other => {
            return (
                None,
                Some(format!(
                    "note: input '{input}' (.{other}) is not an attachable image type \
                     (png/jpg/webp/gif) — the LLM was sent text only"
                )),
            );
        }
    };

    match std::fs::metadata(path) {
        Ok(meta) if meta.len() > MAX_IMAGE_BYTES => {
            return (
                None,
                Some(format!(
                    "note: input '{input}' is {} bytes, over the {MAX_IMAGE_BYTES}-byte \
                     attachment cap — the LLM was sent text only",
                    meta.len()
                )),
            );
        }
        Ok(_) => {}
        Err(e) => {
            return (
                None,
                Some(format!(
                    "note: could not stat input '{input}' ({e}) — the LLM was sent text only"
                )),
            );
        }
    }

    match std::fs::read(path) {
        Ok(bytes) => {
            use base64::Engine;
            let data = base64::engine::general_purpose::STANDARD.encode(&bytes);
            (
                Some(ImageAttachment {
                    media_type: media_type.to_string(),
                    data,
                }),
                None,
            )
        }
        Err(e) => (
            None,
            Some(format!(
                "note: could not read input '{input}' ({e}) — the LLM was sent text only"
            )),
        ),
    }
}

/// Wrap contour-rendered PNG bytes as the §7 edge-map attachment. `None` when the render
/// alone is over the 8 MiB cap — the edge map is best-effort and is dropped silently.
pub fn edge_map_attachment(bytes: &[u8]) -> Option<ImageAttachment> {
    if bytes.len() as u64 > MAX_IMAGE_BYTES {
        return None;
    }
    use base64::Engine;
    Some(ImageAttachment {
        media_type: "image/png".to_string(),
        data: base64::engine::general_purpose::STANDARD.encode(bytes),
    })
}

