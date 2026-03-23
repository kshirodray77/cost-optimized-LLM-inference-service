# Design Document — LLM Gateway

## Overview

A high-performance C++ reverse proxy for LLM APIs that provides unified access, cost optimization, and operational control across multiple LLM providers.

## Design Principles

1. **OpenAI-compatible API surface** — Drop-in replacement. Any OpenAI SDK works.
2. **Provider-agnostic core** — Adding a new provider means implementing one class.
3. **Cost-awareness everywhere** — Every request tracks its dollar cost.
4. **Fail gracefully** — Automatic fallback chain, health scoring, circuit breaking.
5. **Zero-config start** — Set one env var, get a working gateway.

## Request Lifecycle

```
1. HTTP Request arrives at Crow server
2. Parse JSON body → ChatRequest struct
3. Auth middleware: validate API key, load tier limits
4. Rate limiter: check RPM/TPM for this key
5. Cache lookup: hash(model + messages + params) → cached response?
   → HIT: return immediately (0ms latency, $0 cost)
6. Router: pick provider using configured strategy
   → Resolve model aliases (e.g. "fast" → "groq:llama-3.1-8b")
   → Score available providers (health × cost × latency × weight)
   → Build fallback chain
7. Provider: translate request → provider-specific format → HTTP call
   → Retry with exponential backoff on failure
   → On exhaustion: try next in fallback chain
8. Cost tracker: calculate actual cost from token usage + pricing table
9. Cache store: save response for future hits
10. Metrics: record latency, tokens, cost, provider
11. Return OpenAI-compatible JSON response
```

## Smart Routing Algorithm

The `SMART` strategy scores each provider:

```
score = 100
      - (error_rate × 50)           # Penalize unreliable providers
      - min(avg_latency / 100, 30)   # Penalize slow providers
      + (config_weight × 10)         # Respect admin preferences
      - (cost_per_1k × 100)          # Prefer cheaper providers
```

If `error_rate > 0.5`, the provider is marked unavailable and excluded. Health recovers via exponential decay (`error_rate *= 0.95` on each success).

## Cache Key Strategy

Cache key = hash of: `model | role:content | role:content | ... | temperature | max_tokens`

This means:
- Same messages to same model with same params → cache hit
- Different temperature → cache miss (correct, different output expected)
- `match_params: false` in config → only model + messages matter

## Cost Model

Each provider:model pair has pricing (USD per 1K tokens):

```
cost = (input_tokens / 1000) × input_rate
     + (output_tokens / 1000) × output_rate
```

The gateway estimates cost BEFORE routing (for cost-optimized strategy) and records actual cost AFTER response (for analytics). Cache hits report $0 cost.

## Adding a New Provider

1. Create `include/providers/my_provider.h` extending `BaseProvider`
2. Create `src/providers/my_provider.cpp` implementing `chat()` and `supported_models()`
3. Add to `ProviderRegistry::create_provider()` factory
4. Add pricing to `Config::set_default_pricing()`
5. Add to `CMakeLists.txt` GATEWAY_SOURCES

The `BaseProvider` class gives you: HTTP client, retry with backoff, health checks.

## Thread Safety

All shared state is mutex-protected:
- `Cache` — single mutex for LRU list + index map
- `Router` — mutex for health map
- `CostTracker` — mutex for history + spend maps
- `RateLimiter` — mutex for bucket map
- `AuthMiddleware` — mutex for key map

Crow handles concurrency at the connection level. The gateway is designed for multi-threaded serving (default: 4 threads).
