//! The canonical message shape every provider mapping starts from.

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
