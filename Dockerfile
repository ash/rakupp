# Raku++ (rakupp) in a container:
#
#   docker run --rm -it ghcr.io/ash/rakupp                           # REPL
#   docker run --rm ghcr.io/ash/rakupp -e 'say 6 * 7'
#   docker run --rm -v "$PWD:/work" ghcr.io/ash/rakupp script.raku
#
# The image is the RELEASE ARCHIVE, not a build: the same
# rakupp-linux-<arch>.tar.gz every other installer unpacks, downloaded from the
# GitHub release and checked against its published .sha256. So an image and an
# archive of one version cannot differ, and nothing here compiles. To build one
# locally:
#
#   docker build -t rakupp .                                   # latest release
#   docker build --build-arg RAKUPP_VERSION=v5.0.1 -t rakupp .
#
# The build context is not read at all (.dockerignore excludes everything), so
# a checkout full of build directories costs nothing to send.
#
# Debian, not Alpine: the archive is linked against glibc (2.28 and up). curl
# and ca-certificates are what `rakupp install` fetches with and tar is what it
# unpacks with; libffi and libssl are what NativeCall and the OpenSSL-backed
# modules open at run time. No C++ compiler, so `--exe` is not available out of
# the box -- `apt-get install -y g++` in a derived image brings it back, since
# the runtime it links (lib/, include/) is in the image.

ARG DEBIAN=bookworm-slim
FROM debian:${DEBIAN}

ARG RAKUPP_VERSION=latest
ARG TARGETARCH

RUN set -eu; \
    apt-get update; \
    apt-get install -y --no-install-recommends ca-certificates curl libffi8 libssl3; \
    rm -rf /var/lib/apt/lists/*; \
    case "${TARGETARCH:-$(dpkg --print-architecture)}" in \
        amd64) arch=x86_64 ;; \
        arm64) arch=aarch64 ;; \
        *)     echo "no rakupp release archive for ${TARGETARCH:-this architecture}" >&2; exit 1 ;; \
    esac; \
    name="rakupp-linux-$arch.tar.gz"; \
    if [ "$RAKUPP_VERSION" = latest ]; then \
        base='https://github.com/ash/rakupp/releases/latest/download'; \
    else \
        base="https://github.com/ash/rakupp/releases/download/$RAKUPP_VERSION"; \
    fi; \
    cd /tmp; \
    for f in "$name" "$name.sha256"; do \
        curl -fsSL -o "$f" "$base/$f" \
            || { echo "release $RAKUPP_VERSION has no $f" >&2; exit 1; }; \
    done; \
    sha256sum -c "$name.sha256"; \
    mkdir -p /opt/rakupp; \
    tar -xzf "$name" -C /opt/rakupp --strip-components=1; \
    rm -f "$name" "$name.sha256"; \
    ln -s rakupp /opt/rakupp/bin/raku; \
    /opt/rakupp/bin/rakupp --version; \
    test "$(/opt/rakupp/bin/rakupp -e 'say 6 * 7')" = 42

ENV PATH=/opt/rakupp/bin:$PATH

LABEL org.opencontainers.image.title="Raku++" \
      org.opencontainers.image.description="Raku++ (rakupp), a from-scratch Raku implementation in C++" \
      org.opencontainers.image.source="https://github.com/ash/rakupp" \
      org.opencontainers.image.url="https://raku.online" \
      org.opencontainers.image.licenses="Artistic-2.0"

WORKDIR /work
ENTRYPOINT ["rakupp"]
