//! The `stencil_edit` crop: a ready-made spec string or structured edges, each edge a
//! length token or a bare number of pixels.

use schemars::JsonSchema;
use serde::Deserialize;

/// Crop given either as a ready-made spec string or as structured edges.
#[derive(Debug, Deserialize, JsonSchema)]
#[serde(untagged)]
pub enum Crop {
    Spec(String),
    Edges {
        #[serde(default)]
        x1: Option<Edge>,
        #[serde(default)]
        x2: Option<Edge>,
        #[serde(default)]
        y1: Option<Edge>,
        #[serde(default)]
        y2: Option<Edge>,
    },
}

/// One crop edge: a length token (`10%`, `-2cm`, `40px`) or a bare pixel number.
#[derive(Debug, Deserialize, JsonSchema)]
#[serde(untagged)]
pub enum Edge {
    Token(String),
    Pixels(f64),
}

impl Edge {
    fn token(&self) -> String {
        match self {
            Edge::Token(text) => text.trim().to_string(),
            Edge::Pixels(n) if n.fract() == 0.0 && n.abs() < 1e15 => format!("{}", *n as i64),
            Edge::Pixels(n) => n.to_string(),
        }
    }
}

impl Crop {
    /// Render to the `-c` spec string the CLI expects.
    pub fn to_spec(&self) -> String {
        match self {
            Crop::Spec(s) => s.trim().to_string(),
            Crop::Edges { x1, x2, y1, y2 } => [("x1", x1), ("x2", x2), ("y1", y1), ("y2", y2)]
                .iter()
                .filter_map(|(name, edge)| edge.as_ref().map(|v| format!("{name}={}", v.token())))
                .collect::<Vec<_>>()
                .join(" "),
        }
    }
}
