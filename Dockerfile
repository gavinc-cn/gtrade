# 第一阶段 - 编译环境

FROM ubuntu:24.04 AS base
SHELL ["/bin/bash", "-c"]
RUN apt update

RUN set -eux && apt-get update && apt-get install -y --fix-missing --no-install-recommends \
    npm

RUN set -eux && apt-get update && apt-get install -y --fix-missing --no-install-recommends \
    libmpfr6

RUN set -eux && apt-get update && apt-get install -y --fix-missing --no-install-recommends \
    libwebsocketpp-dev \
    libboost-all-dev \
    cmake \
    gdb \
    gcc \
    g++ \
    libyaml-cpp-dev \
    libcrypto++-dev \
    libgtest-dev \
    libgmock-dev \
    libcurl4-openssl-dev \
    libc6-dbg \
    liblz4-dev \
    libssl-dev \
    libabsl-dev \
    vim \
    zip

RUN set -eux && apt-get update && apt-get install -y --fix-missing --no-install-recommends \
    python3-venv \
    python3-full \
    python3-pip

COPY ./requirements.txt /opt/requirements.txt
WORKDIR /opt
RUN python3 -m venv /opt/gtrade_pyenv && \
    echo . /opt/gtrade_pyenv/bin/activate >> ~/.bashrc && \
    . /opt/gtrade_pyenv/bin/activate && \
    pip install -r requirements.txt

RUN set -eux && apt-get update && apt-get install -y --fix-missing --no-install-recommends \
    less \
    iputils-ping \
    telnet \
    nmap \
    openssh-server \
    ninja-build \
    libmysqlclient-dev \
    pkg-config \
    libgrpc++-dev \
    libprotobuf-dev \
    protobuf-compiler-grpc \
    protobuf-compiler \
    tree \
    psmisc \
    curl \
    git

FROM base AS claude_env
# iflow
#RUN bash -c "$(curl -fsSL https://gitee.com/iflow-ai/iflow-cli/raw/main/install.sh)"
# qoder
#RUN curl -fsSL https://qoder.com/install | bash
# codex
RUN npm i -g @openai/codex
# claude code
RUN npm install -g @anthropic-ai/claude-code
RUN echo "alias g-claude='claude --dangerously-skip-permissions'" >> /etc/bash.bashrc


FROM base AS builder
# 复制项目源代码
COPY ./src  /opt/gtrade/src
COPY ./test /opt/gtrade/test
COPY ./CMakeLists.txt  /opt/gtrade/CMakeLists.txt
COPY ./functions.cmake /opt/gtrade/functions.cmake

WORKDIR /opt/gtrade/build
RUN cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make install -j6


# 第二阶段 - 运行环境
FROM base AS app
COPY --from=builder /opt/gtrade/build/output /opt/gtrade
COPY --from=builder /opt/gtrade/src/scripts /opt/gtrade/scripts
WORKDIR /opt/gtrade
RUN ulimit -c unlimited
CMD ["/bin/bash"]
