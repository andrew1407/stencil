package bus

import (
	"bytes"
	"context"
	"log"
	"os"
	"strings"
	"testing"
	"time"
)

// captureLog redirects the std logger for one test.
func captureLog(t *testing.T) *bytes.Buffer {
	t.Helper()
	var buf bytes.Buffer
	flags := log.Flags()
	log.SetOutput(&buf)
	log.SetFlags(0)
	t.Cleanup(func() { log.SetOutput(os.Stderr); log.SetFlags(flags) })
	return &buf
}

// A drop storm must not become a log storm: the first loss warns at once, the
// rest are counted, and the next line past the window reports the backlog.
func TestDropLogRateLimitsAndCountsTheBacklog(t *testing.T) {
	old := dropWindow
	dropWindow = time.Hour
	t.Cleanup(func() { dropWindow = old })
	buf := captureLog(t)

	var d DropLog
	for i := 0; i < 500; i++ {
		d.Drop("bus", "proj:p_a")
	}
	lines := strings.Count(buf.String(), "\n")
	if lines != 1 {
		t.Fatalf("500 drops logged %d lines, want 1: %s", lines, buf.String())
	}
	if !strings.Contains(buf.String(), "dropped 1 message(s)") || !strings.Contains(buf.String(), `"proj:p_a"`) {
		t.Fatalf("first warning should name one drop and the channel: %s", buf.String())
	}

	dropWindow = 0 // the window has passed
	buf.Reset()
	d.Drop("bus", "proj:p_a")
	if !strings.Contains(buf.String(), "dropped 500 message(s)") {
		t.Fatalf("the next line should report the 499 silent drops plus this one: %s", buf.String())
	}
}

// The in-proc bus warns when a subscriber's buffer is full rather than dropping
// in silence.
func TestInProcPublishWarnsOnDrop(t *testing.T) {
	old := dropWindow
	dropWindow = 0
	t.Cleanup(func() { dropWindow = old })
	buf := captureLog(t)

	b := NewInProc()
	_, stop := b.Subscribe("c")
	defer stop()
	for i := 0; i < subBuffer+2; i++ {
		if err := b.Publish(context.Background(), "c", Envelope{Type: "edit"}); err != nil {
			t.Fatal(err)
		}
	}
	if !strings.Contains(buf.String(), "WARN bus: dropped") {
		t.Fatalf("an overflowing subscriber should warn: %q", buf.String())
	}
}
