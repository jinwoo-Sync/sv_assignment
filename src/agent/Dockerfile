# Build stage
FROM ubuntu:20.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

RUN ls -la /build
RUN ls -la /build/src
WORKDIR /build/src/agent
RUN cmake . && make

# Runtime stage
FROM ubuntu:20.04

WORKDIR /app
COPY --from=builder /build/src/agent/agent .

CMD ["./agent", "controller"]
