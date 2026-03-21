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

RUN rm -rf build && cmake -S . -B build && cmake --build build --target controller

# Runtime stage
FROM ubuntu:20.04

WORKDIR /app
COPY --from=builder /build/build/src/controller/controller .
COPY --from=builder /build/build/src/libs/core/libsv_core.so /usr/local/lib/
COPY --from=builder /build/build/src/libs/logger/libsv_logger.so /usr/local/lib/
RUN ldconfig

EXPOSE 9090
CMD ["./controller"]
