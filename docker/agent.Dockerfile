# Build stage
FROM ubuntu:20.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

RUN cmake -B build && cmake --build build --target agent

# Runtime stage
FROM ubuntu:20.04

WORKDIR /app
COPY --from=builder /build/build/agent/agent .

CMD ["./agent", "controller"]
