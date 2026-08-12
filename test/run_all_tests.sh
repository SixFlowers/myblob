#!/usr/bin/env bash
set -euo pipefail

# ==============================================================================
# MyBlob 全量本地测试脚本
#
# 前置条件: 项目已编译 (cd build && cmake .. && make -j$(nproc))
#
# 用法:
#   bash test/run_all_tests.sh            # 全部测试
#   bash test/run_all_tests.sh --quick    # 仅离线单元测试(无需mock server)
#   bash test/run_all_tests.sh --mock-only # 仅启动mock server(手动测试用)
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
MOCK_PID=""

# 颜色
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

pass_count=0
fail_count=0

pass() { echo -e "${GREEN}[PASS]${NC} $1"; pass_count=$((pass_count + 1)); }
fail() { echo -e "${RED}[FAIL]${NC} $1"; fail_count=$((fail_count + 1)); }
info() { echo -e "${YELLOW}[INFO]${NC} $1"; }

cleanup() {
    if [ -n "$MOCK_PID" ] && kill -0 "$MOCK_PID" 2>/dev/null; then
        info "Stopping mock server (pid=$MOCK_PID)..."
        kill "$MOCK_PID" 2>/dev/null || true
        wait "$MOCK_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# ==============================================================================
# 启动 Mock Server
# ==============================================================================
start_mock() {
    info "Starting mock server..."
    python3 "$SCRIPT_DIR/mock_server.py" --http-port 18888 --s3-port 19000 --https-port 18443 &
    MOCK_PID=$!
    sleep 2  # 等待 server 启动

    # 验证 server 已启动
    if ! kill -0 "$MOCK_PID" 2>/dev/null; then
        fail "Mock server failed to start"
        exit 1
    fi
    # 验证端口可连接
    if curl -s http://127.0.0.1:18888/test > /dev/null 2>&1; then
        pass "Mock HTTP server :18888 ready"
    else
        fail "Mock HTTP server :18888 not responding"
    fi
    if curl -s http://127.0.0.1:19000/test > /dev/null 2>&1; then
        pass "Mock S3 server :19000 ready"
    else
        fail "Mock S3 server :19000 not responding"
    fi
    # HTTPS — 用 -k 跳过证书验证
    if curl -sk https://127.0.0.1:18443/test > /dev/null 2>&1; then
        pass "Mock HTTPS server :18443 ready"
    else
        info "Mock HTTPS server :18443 not responding (TLS examples will be skipped)"
    fi
}

# ==============================================================================
# 离线单元测试（无需网络）
# ==============================================================================
run_offline_tests() {
    info "=== 离线单元测试 ==="

    if "$BUILD_DIR/myblob_test" > /dev/null 2>&1; then
        pass "myblob_test (53 tests)"
    else
        fail "myblob_test"
    fi

    if "$BUILD_DIR/quick_test" > /dev/null 2>&1; then
        pass "quick_test"
    else
        fail "quick_test"
    fi
}

# ==============================================================================
# 本地集成测试（需要 mock server）
# ==============================================================================
run_integration_tests() {
    info "=== 集成测试 ==="

    if "$BUILD_DIR/local_http_test" > /dev/null 2>&1; then
        pass "local_http_test (22 tests)"
    else
        fail "local_http_test"
    fi

    if "$BUILD_DIR/cloud_test" > /dev/null 2>&1; then
        pass "cloud_test (36 tests)"
    else
        fail "cloud_test"
    fi

    if "$BUILD_DIR/stress_test" > /dev/null 2>&1; then
        pass "stress_test (12 tests)"
    else
        fail "stress_test"
    fi

    # MinIO test: mock server 支持了 S3 协议
    if "$BUILD_DIR/minio_test" > /dev/null 2>&1; then
        pass "minio_test (8 tests)"
    else
        fail "minio_test"
    fi

    if "$BUILD_DIR/multipart_test" > /dev/null 2>&1; then
        pass "multipart_test (5 tests)"
    else
        fail "multipart_test"
    fi
}

# ==============================================================================
# 修复后的示例（不应崩溃）
# ==============================================================================
run_examples() {
    info "=== 示例程序 ==="

    # 不再崩溃的示例
    if "$BUILD_DIR/providers_example" > /dev/null 2>&1; then
        pass "providers_example"
    else
        fail "providers_example"
    fi

    if "$BUILD_DIR/cloud_example" > /dev/null 2>&1; then
        pass "cloud_example"
    else
        fail "cloud_example"
    fi

    if "$BUILD_DIR/tasked_example" > /dev/null 2>&1; then
        pass "tasked_example"
    else
        fail "tasked_example"
    fi

    if "$BUILD_DIR/multipartUpload_example" > /dev/null 2>&1; then
        pass "multipartUpload_example"
    else
        fail "multipartUpload_example"
    fi

    # 网络示例 — 通过 MYBLOB_ENDPOINT 重定向到本地 mock
    export MYBLOB_ENDPOINT="http://127.0.0.1:18888"

    if timeout 5 "$BUILD_DIR/httpbin_example" > /dev/null 2>&1; then
        pass "httpbin_example"
    else
        fail "httpbin_example"
    fi

    if timeout 5 "$BUILD_DIR/simple_download" > /dev/null 2>&1; then
        pass "simple_download"
    else
        fail "simple_download"
    fi

    if timeout 5 "$BUILD_DIR/batch_example" > /dev/null 2>&1; then
        pass "batch_example"
    else
        fail "batch_example"
    fi

    if timeout 5 "$BUILD_DIR/aws1_example" > /dev/null 2>&1; then
        pass "aws1_example"
    else
        fail "aws1_example"
    fi

    if timeout 5 "$BUILD_DIR/aws3_example" > /dev/null 2>&1; then
        pass "aws3_example"
    else
        fail "aws3_example"
    fi

    # MinIO 示例 — 重定向到 S3 mock
    export MYBLOB_ENDPOINT="minio://127.0.0.1:19000/test1/"

    if timeout 5 "$BUILD_DIR/minio_example" > /dev/null 2>&1; then
        pass "minio_example"
    else
        fail "minio_example"
    fi

    if timeout 5 "$BUILD_DIR/minio_upload_example" > /dev/null 2>&1; then
        pass "minio_upload_example"
    else
        fail "minio_upload_example"
    fi

    if timeout 5 "$BUILD_DIR/minio_delete_example" > /dev/null 2>&1; then
        pass "minio_delete_example"
    else
        fail "minio_delete_example"
    fi

    if timeout 10 "$BUILD_DIR/minio_multipart_example" > /dev/null 2>&1; then
        pass "minio_multipart_example"
    else
        fail "minio_multipart_example"
    fi

    # HTTPS 示例
    export MYBLOB_ENDPOINT="https://127.0.0.1:18443"
    if timeout 5 "$BUILD_DIR/httpbin_https_example" > /dev/null 2>&1; then
        pass "httpbin_https_example"
    else
        fail "httpbin_https_example"
    fi

    if timeout 5 "$BUILD_DIR/aws2_example" > /dev/null 2>&1; then
        pass "aws2_example (HTTPS)"
    else
        fail "aws2_example (HTTPS)"
    fi

    unset MYBLOB_ENDPOINT
}

# ==============================================================================
# Main
# ==============================================================================
echo "╔══════════════════════════════════════════╗"
echo "║     MyBlob 全量本地测试                  ║"
echo "╚══════════════════════════════════════════╝"
echo ""

if [ ! -d "$BUILD_DIR" ]; then
    fail "Build directory not found: $BUILD_DIR"
    echo "Please run: cd $(dirname "$BUILD_DIR") && mkdir build && cd build && cmake .. && make -j\$(nproc)"
    exit 1
fi

case "${1:-}" in
    --quick)
        run_offline_tests
        ;;
    --mock-only)
        start_mock
        info "Mock server running. Press Ctrl+C to stop."
        wait "$MOCK_PID"
        ;;
    *)
        start_mock
        run_offline_tests
        run_integration_tests
        run_examples
        ;;
esac

echo ""
echo "═══════════════════════════════════════════"
echo -e "  总计: $((pass_count + fail_count))  通过: ${GREEN}$pass_count${NC}  失败: ${RED}$fail_count${NC}"
echo "═══════════════════════════════════════════"

if [ "$fail_count" -gt 0 ]; then
    exit 1
fi
exit 0
