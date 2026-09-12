//! URL parsing and the request guards: headers, paths, and where a credential may travel.
use stencil_mcp::llmtransport::{
    guard_credentials, parse_http_url, validate_request_parts, HttpTarget, LlmError,
};

#[test]
fn parses_http_urls() {
    assert_eq!(
        parse_http_url("http://localhost:11434/api/chat").unwrap(),
        HttpTarget {
            host: "localhost".into(),
            port: 11434,
            path: "/api/chat".into()
        }
    );
    // Default port + default path.
    assert_eq!(
        parse_http_url("http://example.com").unwrap(),
        HttpTarget {
            host: "example.com".into(),
            port: 80,
            path: "/".into()
        }
    );
    // Query rides with the path.
    assert_eq!(
        parse_http_url("http://h:1/v1/chat?x=1").unwrap().path,
        "/v1/chat?x=1"
    );
    // Bracketed IPv6.
    assert_eq!(
        parse_http_url("http://[::1]:8090/llm/chat").unwrap(),
        HttpTarget {
            host: "::1".into(),
            port: 8090,
            path: "/llm/chat".into()
        }
    );
    // Bad ports / empty host.
    assert!(matches!(
        parse_http_url("http://host:notaport/x"),
        Err(LlmError::InvalidUrl(_))
    ));
    assert!(matches!(
        parse_http_url("http:///x"),
        Err(LlmError::InvalidUrl(_))
    ));
}

#[test]
fn crlf_in_a_header_or_path_is_refused_before_the_request_is_built() {
    let target = parse_http_url("http://localhost:11434/api/chat").unwrap();

    // A secret that picked up a stray newline must not forge a header line.
    let smuggled = vec![(
        "Authorization".to_string(),
        "Bearer k\r\nX-Injected: yes".to_string(),
    )];
    let err = validate_request_parts(&target, &smuggled).unwrap_err();
    assert!(matches!(err, LlmError::UnsafeHeader(_)), "{err}");
    assert!(err.to_string().contains("Authorization"), "{err}");

    let bad_name = vec![("X-A\r\nX-B".to_string(), "v".to_string())];
    assert!(matches!(
        validate_request_parts(&target, &bad_name).unwrap_err(),
        LlmError::UnsafeHeader(_)
    ));

    let nul = vec![("X-A".to_string(), "v\0w".to_string())];
    assert!(matches!(
        validate_request_parts(&target, &nul).unwrap_err(),
        LlmError::UnsafeHeader(_)
    ));

    let mut bad_path = target.clone();
    bad_path.path = "/api/chat\r\nX-Injected: yes".to_string();
    assert!(matches!(
        validate_request_parts(&bad_path, &[]).unwrap_err(),
        LlmError::UnsafeHeader(_)
    ));

    // The ordinary case still passes.
    let ok = vec![("Authorization".to_string(), "Bearer sk-local".to_string())];
    assert!(validate_request_parts(&target, &ok).is_ok());
}

#[test]
fn credentials_only_travel_to_loopback() {
    let creds = vec![("Authorization".to_string(), "Bearer sk-local".to_string())];
    let api_key = vec![("x-api-key".to_string(), "sk-ant".to_string())];
    let plain = vec![("Content-Type".to_string(), "application/json".to_string())];

    // This transport has no TLS, so an off-box peer would get the secret in cleartext.
    let err = guard_credentials(&creds, "llm.example.com", false).unwrap_err();
    assert!(matches!(err, LlmError::CredentialOffLoopback { .. }), "{err}");
    assert!(err.to_string().contains("loopback"), "{err}");
    assert!(guard_credentials(&api_key, "llm.example.com", false).is_err());

    // Loopback is the supported deployment; non-credential headers are always fine.
    assert!(guard_credentials(&creds, "localhost", true).is_ok());
    assert!(guard_credentials(&plain, "llm.example.com", false).is_ok());

    // An empty credential is not a credential.
    let empty = vec![("Authorization".to_string(), String::new())];
    assert!(guard_credentials(&empty, "llm.example.com", false).is_ok());
}
