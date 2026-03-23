# ═══════════════════════════════════════════════════════════════════════
# LLM Gateway — Multi-stage Docker build
# Final image: ~50MB, no compiler, no source code
# ═══════════════════════════════════════════════════════════════════════

# ── Build stage ──────────────────────────────────────────────────────
FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates \
    libboost-system-dev libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

RUN mkdir -p build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF \
    && make -j$(nproc)

# ── Production stage ─────────────────────────────────────────────────
FROM ubuntu:24.04 AS production

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates libssl3 libboost-system1.83.0 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -r -s /bin/false gateway

WORKDIR /app

COPY --from=builder /build/build/llm_gateway /app/llm_gateway
COPY config/gateway.json /app/config/gateway.json

RUN chown -R gateway:gateway /app

USER gateway

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -sf http://localhost:8080/health || exit 1

ENTRYPOINT ["/app/llm_gateway"]
CMD ["/app/config/gateway.json"]
