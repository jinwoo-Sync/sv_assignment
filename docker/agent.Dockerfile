# Build stage
FROM ubuntu:20.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF && \
    cmake --build build --target agent -- -j$(nproc)

# Runtime stage
FROM ubuntu:20.04

WORKDIR /app
COPY --from=builder /build/bin/Release/agent .
COPY --from=builder /build/bin/Release/libsv_core.so /usr/local/lib/
COPY --from=builder /build/bin/Release/libsv_logger.so /usr/local/lib/
RUN ldconfig

CMD ["./agent", "controller"]
