//! Mapping an op plan onto CLI runs (contract §2, §2.1): action collapse, crop, rotation,
//! filters, layouts, multi-image `image`/`save`, and what cannot map.
#[path = "../common/mod.rs"]
mod common;

mod argv;
mod image;
mod mapping;
mod save;
