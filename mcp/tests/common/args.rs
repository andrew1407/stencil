//! Building the two parameter records from a JSON literal, for the argv-mapping suites.
use stencil_mcp::args::{EditParams, ScrapeParams};

pub fn params(value: serde_json::Value) -> EditParams {
    serde_json::from_value(value).expect("params should deserialize")
}

pub fn scrape_params(value: serde_json::Value) -> ScrapeParams {
    serde_json::from_value(value).expect("scrape params should deserialize")
}
