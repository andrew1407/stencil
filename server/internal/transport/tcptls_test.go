package transport_test

import (
	"context"
	"crypto/tls"
	"net"
	"sync/atomic"
	"testing"
	"time"

	"stencil/server/internal/testutil"
	"stencil/server/internal/transport"
)

// countingConn counts the writes that reach the socket; under TLS each one is a sealed record.
type countingConn struct {
	net.Conn
	writes atomic.Int32
}

func (c *countingConn) Write(b []byte) (int, error) {
	c.writes.Add(1)
	return c.Conn.Write(b)
}

type tlsSide struct {
	raw *countingConn
	tls *tls.Conn
	err error
}

// tlsPair dials a TLS loopback pair as the TLS TCP listener would serve it, handshakes both ends,
// and returns the client and the server end with a write counter under its TLS layer.
func tlsPair(t *testing.T) (*tls.Conn, tlsSide) {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { ln.Close() })
	conf := testutil.SelfSignedTLS(t)
	done := make(chan tlsSide, 1)
	go func() {
		c, err := ln.Accept()
		if err != nil {
			done <- tlsSide{err: err}
			return
		}
		raw := &countingConn{Conn: c}
		srv := tls.Server(raw, conf)
		done <- tlsSide{raw: raw, tls: srv, err: srv.Handshake()}
	}()
	client, err := tls.Dial("tcp", ln.Addr().String(), &tls.Config{InsecureSkipVerify: true, MinVersion: tls.VersionTLS12})
	if err != nil {
		t.Fatalf("tls dial: %v", err)
	}
	server := <-done
	if server.err != nil {
		t.Fatalf("server handshake: %v", server.err)
	}
	t.Cleanup(func() { client.Close(); server.tls.Close() })
	return client, server
}

// On the TLS TCP listener a frame and its delimiter are one Write, so one record, not a frame record
// followed by a one-byte '\n' record.
func TestTCPWriteOverTLSIsOneRecordPerFrame(t *testing.T) {
	client, server := tlsPair(t)
	conn := transport.NewTCP(server.tls)
	reader := transport.NewTCP(client)
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()

	for _, frame := range []string{`{"type":"pong"}`, `{"type":"edit","op":"addLine"}`} {
		before := server.raw.writes.Load()
		if err := conn.Write(ctx, []byte(frame)); err != nil {
			t.Fatalf("write: %v", err)
		}
		if n := server.raw.writes.Load() - before; n != 1 {
			t.Fatalf("frame %s took %d socket writes, want 1", frame, n)
		}
		got, err := reader.Read(ctx)
		if err != nil {
			t.Fatalf("read: %v", err)
		}
		if string(got) != frame {
			t.Fatalf("got %q, want %q", got, frame)
		}
	}
}

// A frame past the kept scratch size still arrives whole, and the next small frame is unaffected.
func TestTCPWriteOverTLSCarriesALargeFrameWhole(t *testing.T) {
	client, server := tlsPair(t)
	conn := transport.NewTCP(server.tls)
	reader := transport.NewTCP(client)
	reader.SetReadLimit(transport.MaxMessageBytes)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	big := make([]byte, 200<<10)
	for i := range big {
		big[i] = 'a' + byte(i%26)
	}
	big[0], big[len(big)-1] = '"', '"'
	for _, frame := range [][]byte{big, []byte(`{"type":"pong"}`)} {
		go func() { _ = conn.Write(ctx, frame) }()
		got, err := reader.Read(ctx)
		if err != nil {
			t.Fatalf("read: %v", err)
		}
		if string(got) != string(frame) {
			t.Fatalf("frame of %d bytes came back as %d bytes", len(frame), len(got))
		}
	}
}
