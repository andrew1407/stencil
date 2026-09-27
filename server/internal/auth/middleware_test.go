package auth

import (
	"context"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

// deadlineResolver records whether the lookup it served carried a deadline.
type deadlineResolver struct{ sawDeadline chan bool }

func (d deadlineResolver) ResolveToken(ctx context.Context, _ []byte) (Session, error) {
	_, ok := ctx.Deadline()
	d.sawDeadline <- ok
	return Session{ID: "s1"}, nil
}

// The per-request token lookup runs under the op timeout, like every other store call, so one stuck
// query cannot hold a pool connection for the whole request.
func TestMiddlewareBoundsTheLookup(t *testing.T) {
	for _, tc := range []struct {
		timeout time.Duration
		want    bool
	}{{time.Second, true}, {0, false}} {
		res := deadlineResolver{sawDeadline: make(chan bool, 1)}
		h := Middleware(res, tc.timeout)(http.HandlerFunc(func(rw http.ResponseWriter, _ *http.Request) {}))
		req := httptest.NewRequest(http.MethodGet, "/projects", nil)
		req.Header.Set("Authorization", "Bearer tok")
		h.ServeHTTP(httptest.NewRecorder(), req)
		if got := <-res.sawDeadline; got != tc.want {
			t.Fatalf("timeout %v: lookup deadline %v, want %v", tc.timeout, got, tc.want)
		}
	}
}
