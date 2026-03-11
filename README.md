Cost Optimized LLM Gateway (C++)

A lightweight C++ gateway that routes requests to different LLM providers
while reducing cost using caching and routing logic.

 Features

- Model routing
- Prompt caching
- Cost tracking
- API gateway for LLM requests

Architecture

Client → API Server → Router → Cache → Model Provider → Response

Project Structure

llm-gateway-cpp
│
├── src
│   ├── main.cpp
│   ├── router.cpp
│   ├── cache.cpp
│   ├── cost_tracker.cpp
│   │
│   └── providers
│        ├── openai_provider.cpp
│        └── together_provider.cpp
│
├── include
│   ├── router.h
│   ├── cache.h
│   ├── cost_tracker.h
│
├── CMakeLists.txt
└── README.md
├── DESIGN.md
└── .gitignore

