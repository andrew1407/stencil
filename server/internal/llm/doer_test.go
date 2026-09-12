package llm

// The outbound HTTP seam every provider test injects.

import (
	"io"
	"net/http"
	"strings"
)

// mockDoer captures the outbound request and replies with a canned response.
type mockDoer struct {
	req    *http.Request
	body   []byte // captured request body
	status int
	resp   string
	err    error
}

func (f *mockDoer) Do(req *http.Request) (*http.Response, error) {
	f.req = req
	if req.Body != nil {
		f.body, _ = io.ReadAll(req.Body)
	}
	if f.err != nil {
		return nil, f.err
	}
	return &http.Response{
		StatusCode: f.status,
		Body:       io.NopCloser(strings.NewReader(f.resp)),
		Header:     http.Header{},
	}, nil
}

func testClient(d Doer) *Client {
	return &Client{provider: ProviderAnthropic, base: "https://api.example.test", key: "sk-secret-key", model: "claude-opus-5", maxTokens: 8192, http: d}
}

const okResponse = `{"model":"claude-opus-5","stop_reason":"end_turn","content":[{"type":"text","text":"hello"}]}`
