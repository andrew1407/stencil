package transport

// The TCP read limit: MaxHelloBytes until SetReadLimit, a refusal that names ErrFrameTooLarge, and a
// final unterminated line still delivered at EOF.

import (
	"context"
	"errors"
	"strings"
	"testing"
	"time"
)

// A fresh TCP Conn refuses a frame past MaxHelloBytes; after SetReadLimit the same frame is read whole.
func TestTCPReadsUnderTheHelloCapUntilRaised(t *testing.T) {
	line := strings.Repeat("a", MaxHelloBytes+1) + "\n"
	client, server := tcpPair(t)
	go func() { _, _ = client.(*tcpConn).conn.Write([]byte(line)) }()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if _, err := server.Read(ctx); !errors.Is(err, ErrFrameTooLarge) {
		t.Fatalf("a frame past MaxHelloBytes read with %v, want ErrFrameTooLarge", err)
	}

	client2, server2 := tcpPair(t)
	server2.SetReadLimit(MaxMessageBytes)
	go func() { _, _ = client2.(*tcpConn).conn.Write([]byte(line)) }()
	if got := read1(t, server2); len(got) != MaxHelloBytes+1 {
		t.Fatalf("after the raise the frame came back %d bytes", len(got))
	}
}

// A frame of exactly the hello cap is legal before the raise.
func TestTCPAcceptsAFrameAtTheHelloCap(t *testing.T) {
	client, server := tcpPair(t)
	payload := strings.Repeat("a", MaxHelloBytes)
	go func() { _, _ = client.(*tcpConn).conn.Write([]byte(payload + "\n")) }()
	if got := read1(t, server); len(got) != MaxHelloBytes {
		t.Fatalf("read %d bytes, want %d", len(got), MaxHelloBytes)
	}
}

// A peer that closes after a line with no delimiter still has that line delivered, then EOF.
func TestTCPDeliversAFinalUnterminatedLine(t *testing.T) {
	client, server := tcpPair(t)
	raw := client.(*tcpConn)
	if _, err := raw.conn.Write([]byte(`{"type":"ping"}`)); err != nil {
		t.Fatal(err)
	}
	if err := client.Close(CloseNormal, ""); err != nil {
		t.Fatal(err)
	}
	if got := string(read1(t, server)); got != `{"type":"ping"}` {
		t.Fatalf("got %q", got)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	if _, err := server.Read(ctx); err == nil {
		t.Fatal("expected EOF after the final line")
	}
}
