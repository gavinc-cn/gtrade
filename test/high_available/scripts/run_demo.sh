#!/bin/bash

# HA Demo 运行脚本

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}/.."
BUILD_DIR="${PROJECT_DIR}/build"
BIN_DIR="${BUILD_DIR}"

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_header() {
    echo ""
    echo -e "${BLUE}=========================================${NC}"
    echo -e "${BLUE}     High Availability Demo${NC}"
    echo -e "${BLUE}=========================================${NC}"
    echo ""
}

print_usage() {
    echo "Usage: $0 <command>"
    echo ""
    echo "Commands:"
    echo "  build       - Build the demo"
    echo "  redis       - Start Redis"
    echo "  node <id>   - Start a HA node (e.g., node_a, node_b)"
    echo "  upstream    - Start upstream simulator"
    echo "  viewer      - Start state viewer"
    echo "  clean       - Clean build directory"
    echo "  test        - Run full test scenario"
    echo ""
    echo "Quick Start:"
    echo "  1. $0 build"
    echo "  2. $0 redis"
    echo "  3. Terminal 1: $0 node node_a"
    echo "  4. Terminal 2: $0 node node_b"
    echo "  5. Terminal 3: $0 upstream"
    echo "  6. Terminal 4: $0 viewer"
    echo ""
}

build() {
    echo -e "${GREEN}Building HA Demo...${NC}"

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    cmake .. -DCMAKE_BUILD_TYPE=Debug
    if [ $? -ne 0 ]; then
        echo -e "${RED}CMake configuration failed${NC}"
        exit 1
    fi

    make -j$(nproc)
    if [ $? -ne 0 ]; then
        echo -e "${RED}Build failed${NC}"
        exit 1
    fi

    echo -e "${GREEN}Build successful!${NC}"
    echo ""
    echo "Executables:"
    ls -la "$BIN_DIR"/ha_node_demo "$BIN_DIR"/upstream_sim "$BIN_DIR"/state_viewer 2>/dev/null
}

start_redis() {
    bash "${SCRIPT_DIR}/start_redis.sh"
}

start_node() {
    local node_id="$1"
    if [ -z "$node_id" ]; then
        echo -e "${RED}Error: Node ID required${NC}"
        echo "Usage: $0 node <node_id>"
        exit 1
    fi

    if [ ! -f "$BIN_DIR/ha_node_demo" ]; then
        echo -e "${RED}Error: ha_node_demo not found. Run '$0 build' first.${NC}"
        exit 1
    fi

    echo -e "${GREEN}Starting HA Node: ${node_id}${NC}"
    "$BIN_DIR/ha_node_demo" "$node_id"
}

start_upstream() {
    if [ ! -f "$BIN_DIR/upstream_sim" ]; then
        echo -e "${RED}Error: upstream_sim not found. Run '$0 build' first.${NC}"
        exit 1
    fi

    echo -e "${GREEN}Starting Upstream Simulator${NC}"
    "$BIN_DIR/upstream_sim"
}

start_viewer() {
    if [ ! -f "$BIN_DIR/state_viewer" ]; then
        echo -e "${RED}Error: state_viewer not found. Run '$0 build' first.${NC}"
        exit 1
    fi

    echo -e "${GREEN}Starting State Viewer${NC}"
    "$BIN_DIR/state_viewer"
}

clean() {
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf "$BUILD_DIR"
    echo "Done"
}

run_test() {
    print_header
    echo -e "${GREEN}Running HA Demo Test Scenario${NC}"
    echo ""

    echo "This test demonstrates:"
    echo "  1. Leader election between two nodes"
    echo "  2. Message processing by the leader"
    echo "  3. Failover when leader crashes"
    echo "  4. Message preservation during failover"
    echo ""

    echo -e "${YELLOW}Please run the following in separate terminals:${NC}"
    echo ""
    echo "  Terminal 1: $0 node node_a"
    echo "  Terminal 2: $0 node node_b"
    echo "  Terminal 3: $0 upstream"
    echo "  Terminal 4: $0 viewer"
    echo ""
    echo "Test steps:"
    echo "  1. Start both nodes - one will become leader"
    echo "  2. In upstream, run: send 5"
    echo "  3. Check viewer to see messages being processed"
    echo "  4. In leader node, type: crash"
    echo "  5. Observe follower becoming leader"
    echo "  6. Check that no messages were lost"
    echo ""
}

# 主程序
case "$1" in
    build)
        build
        ;;
    redis)
        start_redis
        ;;
    node)
        start_node "$2"
        ;;
    upstream)
        start_upstream
        ;;
    viewer)
        start_viewer
        ;;
    clean)
        clean
        ;;
    test)
        run_test
        ;;
    *)
        print_header
        print_usage
        ;;
esac
