//! Where a value sits, for messages: `"x1" in spec`, `"label" in ask.options[2]`

#[derive(Clone, Default)]
pub(super) struct Path {
    root: String,
    key: String,
    container: Option<String>,
}

impl Path {
    pub(super) fn at(root: &str, key: &str) -> Path {
        Path {
            root: root.into(),
            key: key.into(),
            container: None,
        }
    }
    pub(super) fn where_(&self) -> String {
        if self.key.is_empty() {
            return self.root.trim_end_matches('.').to_string();
        }
        let prefix = self
            .container
            .as_ref()
            .map(|c| format!("{c}."))
            .unwrap_or_default();
        format!("{prefix}{}{}", self.root, self.key)
    }
    pub(super) fn label(&self) -> String {
        let suffix = self
            .container
            .as_ref()
            .map(|c| format!(" in {c}"))
            .unwrap_or_default();
        format!("\"{}{}\"{suffix}", self.root, self.key)
    }
    pub(super) fn child(parent: Option<&Path>, key: &str) -> Path {
        match parent {
            Some(p) if !p.key.is_empty() => Path {
                root: String::new(),
                key: key.into(),
                container: Some(p.where_()),
            },
            Some(p) => Path::at(&p.root, key),
            None => Path::at("", key),
        }
    }
    pub(super) fn item(&self, i: usize) -> Path {
        Path {
            root: self.root.clone(),
            key: format!("{}[{i}]", self.key),
            container: self.container.clone(),
        }
    }
}
