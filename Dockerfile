# ==========================================
# STAGE 1: Build Environment
# ==========================================

FROM debian:bookworm AS builder

RUN apt-get update && apt-get install -y build-essential cmake git
RUN apt-get update && apt-get install -y libasio-dev wget tar

# libwebsocketpp-dev is not C++20 compatible
# Fetch the patched develop branch of websocketpp for C++20 compatibility
RUN wget https://github.com/zaphoyd/websocketpp/archive/refs/heads/develop.tar.gz && \
    tar -xzf develop.tar.gz && \
    cp -r websocketpp-develop/websocketpp /usr/include/ && \
    rm -rf develop.tar.gz websocketpp-develop

WORKDIR /app

COPY CMakeLists.txt .
COPY include/ include/
COPY main.cpp .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
RUN cmake --build build --config Release --parallel $(nproc)

# ==========================================
# STAGE 2: Minimal Runtime Environment
# ==========================================
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    libstdc++6 \
    libbrotli1 \
    libssl3 \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -m lob_user
USER lob_user

WORKDIR /app

COPY --from=builder /app/build/lob_engine .
EXPOSE 8000 8080

ENTRYPOINT ["./lob_engine"]
