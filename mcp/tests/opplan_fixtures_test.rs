//! The shared op-plan corpus (`browser/js/config/llm/fixtures/opPlan/`) against core's golden,
//! one reported case each: offline, every mcp result in `generated/normalized.json` is typed,
//! round-tripped and its verdict checked; with a CLI, `--plan-check` must print that golden.

use std::collections::HashMap;
use std::io::Write;
use std::process::{Command, Stdio};
use std::sync::LazyLock;

use base64::Engine;
use serde_json::Value;
use stencil_mcp::opplan::{from_result, OpPlanError};

mod common;
use common::plan::{action_json, canon};
use common::walk::Walk;

const FIXTURES_DIR: &str =
    concat!(env!("CARGO_MANIFEST_DIR"), "/../browser/js/config/llm/fixtures/opPlan");

const PROFILES: [&str; 6] = ["editor", "console", "bot", "mcp", "extension", "all"];
const SURFACES: [&str; 7] = ["browser", "desktop", "cli", "pystencil", "bot", "mcp", "extension"];

/// Every bundle as (label, fixture); the golden's mcp result per case name.
static CORPUS: LazyLock<Vec<(String, Value)>> = LazyLock::new(load_corpus);
static GOLDEN: LazyLock<HashMap<String, String>> = LazyLock::new(load_golden);

fn read(rel: &str) -> Value {
    common::read_json(&format!("{FIXTURES_DIR}/{rel}"))
}

fn bundle(rel: &str) -> Vec<Value> {
    read(rel)["cases"].as_array().unwrap_or_else(|| panic!("{rel} has no cases array")).clone()
}

fn load_corpus() -> Vec<(String, Value)> {
    // Hand-written cases keep their "file" label; generated and oracle ones walk as "<name>.json".
    let label = |fx: &Value| match fx["file"].as_str() {
        Some(file) => file.to_owned(),
        None => format!("{}.json", fx["name"].as_str().expect("a case name")),
    };
    ["cases.json", "generated/cases.json", "oracle/inputs.json"]
        .into_iter()
        .flat_map(bundle)
        .map(|fx| (label(&fx), fx))
        .collect()
}

fn load_golden() -> HashMap<String, String> {
    let cases = read("generated/normalized.json")["cases"].as_array().cloned().unwrap_or_default();
    let mcp = |r: &&Value| r["surfaces"].as_array().is_some_and(|s| s.iter().any(|s| s == "mcp"));
    cases
        .iter()
        .filter_map(|c| {
            let result = c["results"].as_array()?.iter().find(mcp)?;
            Some((c["name"].as_str()?.to_owned(), result["json"].as_str()?.to_owned()))
        })
        .collect()
}

/// The corpus-shape check: one case, and a malformed fixture names itself.
fn corpus_is_well_formed() {
    // Floored per bundle: the generated cases alone would clear a combined floor.
    let hand = bundle("cases.json").len();
    let generated = bundle("generated/cases.json").len();
    assert!(hand >= 180, "hand-written cases.json collapsed to {hand}");
    assert!(generated >= 400, "generated/cases.json collapsed to {generated}");
    for (file, fx) in CORPUS.iter().filter(|(_, fx)| !fx["profiles"].is_null()) {
        let slug = match file.split_once('-') {
            Some((num, rest)) if num.chars().all(|c| c.is_ascii_digit()) => rest,
            _ => file.as_str(),
        };
        assert_eq!(format!("{}.json", fx["name"].as_str().unwrap_or_default()), slug, "{file}");
        let profiles = fx["profiles"].as_array().filter(|p| !p.is_empty());
        let profiles = profiles.unwrap_or_else(|| panic!("{file}: \"profiles\" must be non-empty"));
        assert!(profiles.iter().all(|p| p.as_str().is_some_and(|p| PROFILES.contains(&p))), "{file}");
        let expect = fx["expect"].as_str();
        assert!(matches!(expect, Some("valid" | "invalid")), "{file}: \"expect\" must be valid|invalid");
        assert!(!fx["input"].is_null(), "{file}: \"input\" is required");
        if expect == Some("invalid") {
            assert!(fx["reason"].as_str().is_some_and(|r| !r.is_empty()), "{file}: needs a \"reason\"");
        }
        for (surface, verdict) in fx["knownDivergence"].as_object().into_iter().flatten() {
            assert!(SURFACES.contains(&surface.as_str()), "{file}: unknown surface \"{surface}\"");
            assert!(matches!(verdict.as_str(), Some("valid" | "invalid")), "{file}: {verdict}");
        }
    }
}

/// A case applies when its profiles name mcp (or all); an oracle input names none and applies.
fn applies(fx: &Value) -> bool {
    fx["profiles"]
        .as_array()
        .is_none_or(|p| p.iter().any(|p| matches!(p.as_str(), Some("mcp" | "all"))))
}

/// The golden every walked case must have, and nothing the corpus does not hold.
fn every_mcp_case_has_its_golden() {
    let walked: Vec<&str> = CORPUS.iter().filter(|(_, fx)| applies(fx)).filter_map(|(_, fx)| fx["name"].as_str()).collect();
    assert!(walked.len() >= 300, "expected many mcp cases, walked {}", walked.len());
    let missing: Vec<_> = walked.iter().filter(|n| !GOLDEN.contains_key(**n)).collect();
    assert!(missing.is_empty(), "no mcp golden for {missing:?}: run `npm run gen-fixtures` in browser/");
    assert_eq!(GOLDEN.len(), walked.len(), "the golden names cases the corpus does not walk for mcp");
}

/// Verdict precedence: local override > knownDivergence.mcp > expect; then the typed plan,
/// written back, must be the golden's own actions, variants, ask and warnings.
fn check_typed(fx: &Value) {
    let name = fx["name"].as_str().unwrap_or_default();
    let golden: Value = serde_json::from_str(&GOLDEN[name]).expect("a golden result");
    // An oracle input's `expect` is the reference surface's verdict; for mcp the golden is.
    let oracle = if golden["status"] == "invalid" { "invalid" } else { "valid" };
    let want = common::overrides("opPlan")[name]["verdict"]
        .as_str()
        .or_else(|| fx["knownDivergence"]["mcp"].as_str())
        .or_else(|| fx["profiles"].is_array().then(|| fx["expect"].as_str()).flatten())
        .unwrap_or(oracle);
    let typed = from_result(&GOLDEN[name]);
    assert_eq!(if typed.is_ok() { "valid" } else { "invalid" }, want, "{}", GOLDEN[name]);
    let plan = match typed {
        Ok(plan) => plan,
        Err(err) => {
            let action = golden["error"]["code"] == "E_ACTION";
            assert_eq!(matches!(err, OpPlanError::Action { .. }), action, "{err}");
            return;
        }
    };
    let actions = |list: &Value| list.as_array().into_iter().flatten().map(canon).collect::<Vec<_>>();
    assert_eq!(plan.chat_only, golden["status"] == "chatOnly");
    assert_eq!(plan.reply, golden["reply"].as_str().unwrap_or_default());
    assert_eq!(plan.actions.iter().map(action_json).collect::<Vec<_>>(), actions(&golden["actions"]));
    let variants = golden["variants"].as_array().cloned().unwrap_or_default();
    assert_eq!(plan.variants.len(), variants.len());
    for (typed, gold) in plan.variants.iter().zip(&variants) {
        assert_eq!(typed.label, gold["label"].as_str().unwrap_or_default());
        assert_eq!(typed.actions.iter().map(action_json).collect::<Vec<_>>(), actions(&gold["actions"]));
    }
    let options = golden["ask"]["options"].as_array().cloned().unwrap_or_default();
    let previews = options.iter().any(|o| !o["actions"].is_null() || !o["image"].is_null());
    let warnings = golden["warnings"].as_array().map_or(0, Vec::len);
    let dropped = golden["warnings"].as_array().into_iter().flatten().any(|w| w["code"] == "W_PREVIEW_DROPPED");
    assert_eq!(plan.warnings.len(), warnings + usize::from(previews || dropped), "{:?}", plan.warnings);
    match plan.ask {
        Some(card) => {
            assert_eq!(card.question, golden["ask"]["question"].as_str().unwrap_or_default());
            assert_eq!(card.multi, golden["ask"]["mode"] == "multi");
            assert_eq!(card.allow_custom, golden["ask"]["allowCustom"] == true);
            let labels: Vec<_> = options.iter().map(|o| o["label"].as_str().unwrap_or_default()).collect();
            assert_eq!(card.options.iter().map(|o| o.label.as_str()).collect::<Vec<_>>(), labels);
        }
        None => assert!(golden["ask"].is_null()),
    }
}

/// A case's text as a model reply arrives (the walkers' shared `caseText`).
fn text_of(fx: &Value) -> Vec<u8> {
    if let Some(parts) = fx["parts"].as_array() {
        let chunk = |p: &Value| p[0].as_str().unwrap_or_default().repeat(p[1].as_u64().unwrap_or(0) as usize);
        return parts.iter().map(chunk).collect::<String>().into_bytes();
    }
    if let Some(b64) = fx["inputBase64"].as_str() {
        let raw = base64::engine::general_purpose::STANDARD.decode(b64).expect("base64 input");
        return String::from_utf8_lossy(&raw).into_owned().into_bytes();
    }
    match &fx["input"] {
        Value::String(s) => s.clone().into_bytes(),
        other => serde_json::to_vec(other).expect("an input object"),
    }
}

/// `--plan-check - --plan-surface mcp` prints exactly the golden, in this server's registry.
fn check_cli(bin: &std::path::Path, fx: &Value) {
    let name = fx["name"].as_str().unwrap_or_default();
    let mut child = Command::new(bin)
        .args(stencil_mcp::opplan::PLAN_CHECK_ARGV)
        .env("NO_COLOR", "1")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("the CLI spawns");
    child.stdin.take().expect("a stdin pipe").write_all(&text_of(fx)).expect("the reply is written");
    let out = child.wait_with_output().expect("the CLI finishes");
    let (bytes, fnv) = stencil_mcp::registry::fingerprint();
    let want = format!(
        "{{\"version\":1,\"surface\":\"mcp\",\"registryBytes\":{bytes},\"registryFnv1a64\":\"{fnv}\",\"result\":{}}}\n",
        GOLDEN[name]
    );
    assert_eq!(String::from_utf8_lossy(&out.stdout), want, "{}", String::from_utf8_lossy(&out.stderr));
}

fn main() {
    let mut walk = Walk::new();
    walk.case("corpus_is_well_formed", corpus_is_well_formed);
    walk.case("every_mcp_case_has_its_golden", every_mcp_case_has_its_golden);
    let cases: Vec<&'static (String, Value)> = CORPUS.iter().filter(|(_, fx)| applies(fx)).collect();
    for &(ref file, ref fx) in cases.iter().copied() {
        walk.case(format!("mcp/{file}"), move || check_typed(fx));
    }
    match stencil_mcp::locate::find_cli() {
        Ok(bin) => {
            for &(ref file, ref fx) in cases.iter().copied() {
                let bin = bin.clone();
                walk.case(format!("cli/{file}"), move || check_cli(&bin, fx));
            }
        }
        Err(_) => eprintln!("skipping cli/*: stencil CLI not found (build it in cli/ or set STENCIL_CLI)"),
    }
    walk.run()
}
