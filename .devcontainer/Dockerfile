FROM mcr.microsoft.com/devcontainers/cpp:1-debian-12

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl-dev \
    libboost-system1.81-dev \
    libboost-test1.81-dev \
    libboost-program-options1.81-dev \
    libboost-coroutine1.81-dev \
    libboost-log1.81-dev \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*
