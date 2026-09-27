//! `stencil_projects` through a real `tools/call`: the allowlist and id refusals need no CLI;
//! the reads run the real CLI (`--list-projects` / `--project-info`) against a scripted
//! loopback server and self-skip without it — the operator token, a self-issued session,
//! metadata only, paging, and no redirects.

mod common;
use common::dispatch::{payload_of, text_of, wire, Harness};
use common::e2e::cli_present;
use common::rest::{harness, ok, scripted};

use std::time::Duration;

use serde_json::json;

#[tokio::test]
async fn a_listing_uses_the_operator_token_and_pages_by_id() {
    if !cli_present() {
        return;
    }
    let project = json!({ "id": "p_a_b", "name": "Plans", "imageW": 800, "imageH": 600,
                          "ownerSession": "s-internal", "resultPath": "x/y.png" });
    let later = json!({ "id": "p_c_d", "name": "Shots" });
    let (origin, requests) = scripted(vec![
        ok(json!({ "sessionId": "s1", "expiresAt": 1 })),
        ok(json!({ "projects": [project.clone()], "nextCursor": "c1" })),
        ok(json!({ "projects": [later.clone()] })),
    ]);
    let h = harness(&origin, Some("tok"));
    let result = h.call("stencil_projects", json!({ "limit": 1 })).await.unwrap();

    assert_eq!(wire(&result)["isError"], false, "{}", text_of(&result));
    let payload = payload_of(&result);
    assert_eq!(payload["server"], origin);
    assert_eq!(payload["next_cursor"], "p_a_b");
    assert_eq!(payload["projects"][0], json!({ "id": "p_a_b", "name": "Plans", "createdAt": 0,
        "updatedAt": 0, "hasImage": false, "imageW": 800, "imageH": 600, "version": 0 }));
    assert!(text_of(&result).contains("\"Plans\" (p_a_b) 800x600"), "{}", text_of(&result));
    let probe = requests.recv().unwrap();
    assert!(probe.starts_with("get /auth/session http/1.1"), "{probe}");
    assert!(probe.contains("authorization: bearer tok\r\n"), "{probe}");
    assert!(requests.recv().unwrap().starts_with("get /projects?limit=500 http/1.1"));
    assert!(requests.recv().unwrap().starts_with("get /projects?limit=500&after=c1 http/1.1"));

    let (origin, _) = scripted(vec![
        ok(json!({ "sessionId": "s1", "expiresAt": 1 })),
        ok(json!({ "projects": [project, later] })),
    ]);
    let next = json!({ "limit": 1, "after": "p_a_b" });
    let result = harness(&origin, Some("tok")).call("stencil_projects", next).await.unwrap();
    let payload = payload_of(&result);
    assert_eq!(payload["projects"][0]["id"], "p_c_d");
    assert_eq!(payload["next_cursor"], serde_json::Value::Null);
}

/// With no operator token the CLI mints its own session, and a read by id keeps only the
/// metadata — never the stored image.
#[tokio::test]
async fn a_read_by_id_self_issues_a_session_and_drops_the_payload() {
    if !cli_present() {
        return;
    }
    let (origin, requests) = scripted(vec![
        ok(json!({ "token": "minted", "expiresAt": 1 })),
        ok(json!({ "projects": [{ "id": "p_a_b", "name": "Plans",
                                  "originalContent": "data:image/png;base64,AAAA" }] })),
    ]);
    let h = harness(&origin, None);
    let result = h.call("stencil_projects", json!({ "id": "p_a_b" })).await.unwrap();

    assert_eq!(wire(&result)["isError"], false, "{}", text_of(&result));
    assert_eq!(payload_of(&result)["projects"][0]["name"], "Plans");
    assert!(!wire(&result).to_string().contains("base64"), "the image rode along");
    assert!(requests.recv().unwrap().starts_with("post /auth/token http/1.1"));
    let list = requests.recv().unwrap();
    assert!(list.contains("authorization: bearer minted\r\n"), "{list}");
}

#[tokio::test]
async fn a_redirect_is_an_error_and_is_never_followed() {
    if !cli_present() {
        return;
    }
    let redirect = "HTTP/1.1 302 Found\r\nLocation: http://elsewhere.test/\r\nConnection: close\r\n\
                    Content-Length: 0\r\n\r\n";
    let (origin, requests) = scripted(vec![redirect.to_string(), ok(json!({ "projects": [] }))]);
    let result = harness(&origin, Some("tok")).call("stencil_projects", json!({})).await.unwrap();

    assert_eq!(wire(&result)["isError"], true);
    let message = text_of(&result).to_ascii_lowercase();
    assert!(message.starts_with("error:") && message.contains("redirect"), "{message}");
    requests.recv().unwrap();
    let followed = requests.recv_timeout(Duration::from_millis(300)).is_ok();
    assert!(!followed, "the redirect was followed");
}

#[tokio::test]
async fn a_server_off_the_allowlist_a_bad_id_and_a_bad_limit_are_refused_unasked() {
    let h = harness("http://127.0.0.1:9", None);
    let elsewhere = json!({ "server": "http://attacker.test" });
    let off = h.call("stencil_projects", elsewhere).await.unwrap();
    assert!(text_of(&off).contains("is not an allowed server"), "{}", text_of(&off));
    let bad = h.call("stencil_projects", json!({ "id": "../admin" })).await.unwrap();
    assert!(text_of(&bad).contains("is not a project id"), "{}", text_of(&bad));
    let big = h.call("stencil_projects", json!({ "limit": 501 })).await.unwrap();
    assert!(text_of(&big).contains("`limit` must be 1..500"), "{}", text_of(&big));
    let none = Harness::new().call("stencil_projects", json!({})).await.unwrap();
    assert!(text_of(&none).contains("none is configured"), "{}", text_of(&none));
}
