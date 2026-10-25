package hub

// The whole-server cap beside the per-IP one, and the refusal summary that replaces a log line per socket.

import (
	"bytes"
	"errors"
	"log"
	"strings"
	"testing"
	"time"
)

func TestServerCapRefusesPastTheTotal(t *testing.T) {
	c := newIPCounter(2, 3)
	for _, ip := range []string{"a", "a", "b"} {
		if err := c.take(ip); err != nil {
			t.Fatalf("take %s: %v", ip, err)
		}
	}
	if err := c.take("c"); !errors.Is(err, errServerFull) {
		t.Fatalf("a fourth connection: %v, want the server cap", err)
	}
	c.drop("b")
	if err := c.take("a"); !errors.Is(err, errTooManyConns) {
		t.Fatalf("a third from one address: %v, want the per-IP cap", err)
	}
	if err := c.take("c"); err != nil {
		t.Fatalf("a freed slot should admit another address: %v", err)
	}
}

func TestDefaultServerCapAndItsOption(t *testing.T) {
	if got := newTestHub(t).perIP.total; got != defaultMaxConns {
		t.Fatalf("default server cap %d, want %d", got, defaultMaxConns)
	}
	if h := newTestHub(t, WithMaxConns(0), WithMaxConnsPerIP(0)); h.perIP.take("x") != nil || h.perIP.total != 0 {
		t.Fatal("WithMaxConns(0) should lift the server cap")
	}
}

func TestRefusalsAreSummedNotLoggedEach(t *testing.T) {
	var out bytes.Buffer
	prev := log.Writer()
	log.SetOutput(&out)
	t.Cleanup(func() { log.SetOutput(prev) })
	r := refusalLog{every: time.Minute}
	t0 := time.Unix(1000, 0)
	for i := 0; i < 50; i++ {
		if !r.refused(errHelloThrottled, t0.Add(time.Duration(i)*time.Second/10)) {
			t.Fatal("a throttled hello is a refusal")
		}
	}
	if out.Len() != 0 {
		t.Fatalf("logged inside the window: %q", out.String())
	}
	r.refused(errTooManyConns, t0.Add(time.Minute))
	if got := out.String(); strings.Count(got, "\n") != 1 || !strings.Contains(got, "51 connections refused") {
		t.Fatalf("one summary line expected, got %q", got)
	}
	if r.refused(errors.New("peer went away"), t0) {
		t.Fatal("an ordinary end is not a refusal")
	}
}
