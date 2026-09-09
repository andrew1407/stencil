# Build the Stencil collaboration server. Build context is the server/ directory:
#   docker build -t stencil-server server/
# (Unlike browser/ and cli/, this image does NOT compile core/, so it needs no
# repo-root context — the server is a standalone Go module.)

FROM golang:1.26 AS build
WORKDIR /src
COPY go.mod ./
# Warm this image layer's own module cache — never a host cache. (Local dev vendors into
# server/vendor/ via `go mod vendor`; that dir is git-ignored and excluded from the build
# context by .dockerignore, so the image always uses this clean download.)
RUN go mod download
COPY . .
# go.sum is not tracked either, and `go mod download` above leaves it incomplete, so the
# checksums have to be written here — after the source is in, since tidy reads the imports.
RUN go mod tidy
RUN CGO_ENABLED=0 GOOS=linux go build -trimpath -ldflags="-s -w" -o /out/stencil-server ./cmd/stencil-server

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /out/stencil-server /usr/local/bin/stencil-server
# HTTP/WS and raw-TCP edit ports. Plaintext by default (dev); for anything
# beyond localhost mount a cert/key and set TLS_CERT/TLS_KEY — one pair secures
# HTTPS + WSS and the TCP edit channel (see README "Security").
EXPOSE 8090 8091
VOLUME ["/data"]
ENV FILESTORE_ROOT=/data/filestore
ENTRYPOINT ["/usr/local/bin/stencil-server"]
