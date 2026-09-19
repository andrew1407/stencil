package transport

import (
	"context"
	"net"
	"testing"
	"time"
)

// The hub is written against Conn, so both adapters must behave identically: one message per Read/Write,
// a hard size cap, and a Read that unblocks on context cancellation. The TCP framing is what these pin.

// tcpPair returns two ends of a live TCP connection, both wrapped as Conn.
func tcpPair(t *testing.T) (client, server Conn) {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()

	accepted := make(chan net.Conn, 1)
	go func() {
		c, err := ln.Accept()
		if err != nil {
			close(accepted)
			return
		}
		accepted <- c
	}()

	c, err := dialTCP(ln.Addr().String())
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { c.Close(CloseNormal, "") })

	sc, ok := <-accepted
	if !ok {
		t.Fatal("accept failed")
	}
	s := NewTCP(sc)
	t.Cleanup(func() { s.Close(CloseNormal, "") })
	return c, s
}

// read1 reads one message with a short deadline so a hung test fails fast.
func read1(t *testing.T, c Conn) []byte {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	data, err := c.Read(ctx)
	if err != nil {
		t.Fatalf("read: %v", err)
	}
	return data
}

func write1(t *testing.T, c Conn, s string) {
	t.Helper()
	if err := c.Write(context.Background(), []byte(s)); err != nil {
		t.Fatalf("write: %v", err)
	}
}

// ----- WebSocket adapter -----
