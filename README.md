# Cost-Optimized LLM Gateway

A C++ inference gateway that sits between your application and multiple LLM providers (OpenAI, Together, etc.) and reduces spend through prompt caching, model routing, and per-request cost accounting.

> **Why a gateway instead of calling providers directly?** Production LLM workloads waste money in three ways: (1) re-running identical or near-identical prompts, (2) sending easy requests to expensive frontier models, and (3) having no visibility into where the spend actually goes. This service addresses all three behind a single OpenAI-compatible endpoint, so existing clients work without code changes.

---

## Results

> Replace these numbers with measurements from your own benchmark run before publishing. See [Benchmarks](#benchmarks) for the methodology.

On a workload of `<N>` requests sampled from `<dataset>`:

| Metric | Direct to provider | Through gateway | Delta |
|---|---|---|---|
| Total spend (USD) | `<$X>` | `<$Y>` | `<-Z%>` |
| p50 latency | `<a ms>` | `<b ms>` | `<+/- c ms>` |
| p99 latency | `<a ms>` | `<b ms>` | `<+/- c ms>` |
| Cache hit rate | — | `<H%>` | — |
| Requests routed to cheap tier | — | `<R%>` | — |

Cost reduction comes primarily from `<cache hits | small-model routing | both>`. Latency overhead from the gateway is `<X ms>` at p50, dominated by `<cache lookup | router decision>`.

---

## Architecture ┌──────────────┐
client ─┤  HTTP server ├─► Router ─► Cache ─┬─► provider: OpenAI
└──────────────┘                    ├─► provider: Together
└─► provider: <local>
│
└─► Cost tracker (async)
**Request flow:**

1. **HTTP server** accepts OpenAI-compatible `/v1/chat/completions` requests.
2. **Router** classifies the request (token count, structured output, instruction complexity heuristics) and picks a provider/model tier.
3. **Cache** checks for a hit on a normalized prompt hash. On hit, returns immediately and skips provider call entirely.
4. On miss, the chosen provider is called; response is streamed back to the client and written to cache asynchronously.
5. **Cost tracker** records token counts, provider, model, and resolved $ cost for every request, exposed via `/metrics`.

See [`DESIGN.md`](DESIGN.md) for the rationale behind each component.

---

## Key design decisions

**Why C++ instead of Python/Go?** The gateway is on the hot path of every request. C++ lets the per-request overhead stay under `<X> ms` even with cache lookups and routing logic, which matters when the goal is to *reduce* total latency budget, not eat into it.

**Cache key normalization.** Prompts are normalized (whitespace collapse, system-prompt extraction, optional semantic embedding hash) before hashing, so two functionally identical requests hit the same cache entry. Trade-off: aggressive normalization risks returning a stale answer to a *slightly* different prompt — see [`include/cache.h`](include/cache.h) for the knobs.

**Routing policy.** The default router uses a simple rules-based classifier (prompt length + keyword heuristics) that picks between a "cheap" and "smart" tier. This was a deliberate choice over a learned classifier — the rules-based version is auditable, has zero training data requirement, and a learned router can be added later as a strategy plugin.

**Provider abstraction.** Each provider implements a single `Provider` interface (`generate`, `cost_per_token`, `name`). Adding a new provider is one file in `src/providers/`.

---

## Quick start

### Prerequisites
- C++17 or later (tested with `<gcc 11+ / clang 14+>`)
- CMake 3.16+
- `<libcurl, nlohmann/json, ...>`

### Build
```bash
git clone https://github.com/kshirodray77/cost-optimized-LLM-inference-service.git
cd cost-optimized-LLM-inference-service
cmake -B build
cmake --build build
```

### Run
```bash
export OPENAI_API_KEY=sk-...
export TOGETHER_API_KEY=...
./build/llm-gateway --config config.toml
```

### Send a request
```bash
curl http://localhost:8080/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "model": "auto",
    "messages": [{"role": "user", "content": "What is the capital of France?"}]
  }'
```

`"model": "auto"` lets the router pick. You can also force a specific model.

---

## Configuration

Configuration lives in `config.toml`:

```toml
[server]
port = 8080

[cache]
backend = "in_memory"   # or "redis"
ttl_seconds = 3600
max_entries = 10000

[router]
policy = "rules"        # or "passthrough"
cheap_tier = "together/llama-3.1-8b"
smart_tier = "openai/gpt-4o"

[providers.openai]
api_key_env = "OPENAI_API_KEY"

[providers.together]
api_key_env = "TOGETHER_API_KEY"
```

---

## Benchmarks

> Reproducible. The benchmark harness lives in `bench/`.

```bash
./bench/run.sh --workload mtbench --requests 500 --concurrency 8
```

**Workloads tested:**
- `<MTBench / a sampled prod-like dataset / synthetic mix>` — describe what's in it and why it represents real traffic.

**What's measured:**
- Total provider spend (computed from token counts × published rates)
- End-to-end latency from client perspective (p50 / p95 / p99)
- Gateway-internal overhead (cache lookup, router decision)
- Cache hit rate and routing distribution

**Hardware:** `<CPU, RAM, network>`. Single instance, no horizontal scaling.

Raw benchmark output: [`bench/results/`](bench/results/).

---

## What's done

- [x] HTTP server with OpenAI-compatible endpoint
- [x] OpenAI provider
- [x] Together provider
- [x] In-memory prompt cache with TTL
- [x] Rules-based router
- [x] Per-request cost tracking
- [ ] Redis-backed cache (in progress)
- [ ] Streaming response support
- [ ] Semantic-similarity cache (embedding-hash bucket)
- [ ] Prometheus `/metrics` endpoint
- [ ] Docker image + docker-compose for the full stack

This is an **active learning project** — I'm using it to deepen my understanding of LLM serving economics and high-performance C++ networking. Issues and PRs welcome.

---

## What I learned building this

> A short, honest paragraph here is one of the highest-signal things on the whole page. Recruiters read it. Replace this with 3–5 sentences on the most surprising/hardest thing you ran into — for example: where you initially over-engineered, a benchmark result that contradicted your assumption, a debugging story, or a tradeoff you'd revisit. Specific > generic.

---

## License

MIT
