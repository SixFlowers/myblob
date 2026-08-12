#!/usr/bin/env python3
"""
MyBlob Mock Server — 本地测试用的 S3/HTTP/HTTPS mock 服务器

端口:
  :18888 — 纯 HTTP GET (用于 local_http_test + httpbin 示例)
  :19000 — S3 mock (PUT/DELETE/GET + multipart upload XML)
  :18443 — HTTPS (自签名证书, 客户端不验证证书)

前置: openssl 生成自签名证书
  openssl req -x509 -newkey rsa:2048 -keyout /tmp/key.pem -out /tmp/cert.pem \
      -days 365 -nodes -subj "/CN=localhost"

用法:
  python3 mock_server.py [--http-port 18888] [--s3-port 19000] \
                         [--https-port 18443] [--cert /tmp/cert.pem] [--key /tmp/key.pem]
"""

import argparse
import hashlib
import http.server
import json
import os
import ssl
import sys
import threading
import time
import uuid
from http import HTTPStatus
from urllib.parse import urlparse, parse_qs


# ==============================================================================
# In-memory object store
# ==============================================================================
class ObjectStore:
    """简单的内存对象存储，支持 PUT/GET/DELETE"""
    def __init__(self):
        self._objects: dict[str, tuple[bytes, dict]] = {}  # key -> (data, metadata)

    def put(self, key: str, data: bytes, metadata: dict | None = None) -> str:
        """存储对象，返回 ETag"""
        etag = hashlib.md5(data).hexdigest()
        self._objects[key] = (data, {"etag": etag, **(metadata or {})})
        return etag

    def get(self, key: str) -> tuple[bytes, dict] | None:
        """获取对象，返回 (data, metadata) 或 None"""
        return self._objects.get(key)

    def delete(self, key: str) -> bool:
        """删除对象，返回是否成功"""
        if key in self._objects:
            del self._objects[key]
            return True
        return False

    def exists(self, key: str) -> bool:
        return key in self._objects


class MultipartState:
    """管理多部分上传的状态"""
    def __init__(self):
        self._uploads: dict[str, dict] = {}  # uploadId -> {key, parts: [(etag, size)]}

    def initiate(self, key: str) -> str:
        upload_id = str(uuid.uuid4())
        self._uploads[upload_id] = {"key": key, "parts": []}
        return upload_id

    def upload_part(self, upload_id: str, data: bytes) -> str | None:
        if upload_id not in self._uploads:
            return None
        etag = hashlib.md5(data).hexdigest()
        self._uploads[upload_id]["parts"].append((etag, len(data)))
        return etag

    def complete(self, upload_id: str, store: ObjectStore) -> str | None:
        if upload_id not in self._uploads:
            return None
        upload = self._uploads[upload_id]
        combined = b"".join(
            store.get(f"__part_{upload_id}_{i}")[0]
            for i in range(len(upload["parts"]))
        )
        etag = store.put(upload["key"], combined)
        # 清理临时分片
        for i in range(len(upload["parts"])):
            store.delete(f"__part_{upload_id}_{i}")
        del self._uploads[upload_id]
        return etag

    def abort(self, upload_id: str, store: ObjectStore):
        if upload_id in self._uploads:
            upload = self._uploads[upload_id]
            for i in range(len(upload["parts"])):
                store.delete(f"__part_{upload_id}_{i}")
            del self._uploads[upload_id]


# ==============================================================================
# HTTP Request Handler
# ==============================================================================
class MockHTTPHandler(http.server.BaseHTTPRequestHandler):
    """基础 HTTP GET handler — 用于 :18888"""

    def do_GET(self):
        body = json.dumps({
            "url": self.path,
            "headers": dict(self.headers),
            "origin": self.client_address[0],
            "args": parse_qs(urlparse(self.path).query),
            "uuid": str(uuid.uuid4()),
        }).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        if os.environ.get("MOCK_VERBOSE"):
            super().log_message(format, *args)


class S3MockHandler(http.server.BaseHTTPRequestHandler):
    """
    S3-compatible mock handler — 用于 :19000

    支持:
      GET /<bucket>/<key>          → 200 + body | 404
      PUT /<bucket>/<key>          → 200 + ETag
      DELETE /<bucket>/<key>       → 204
      POST /<bucket>/<key>?uploads → 200 + XML (InitiateMultipartUpload)
      PUT  /<bucket>/<key>?partNumber=N&uploadId=X → 200 + ETag
      POST /<bucket>/<key>?uploadId=X → 200 + XML (CompleteMultipartUpload)
      DELETE /<bucket>/<key>?uploadId=X → 204 (AbortMultipartUpload)
    """

    store = ObjectStore()
    multipart = MultipartState()

    def _send_xml(self, code: int, body: str):
        data = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/xml")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _get_key(self) -> str:
        """从 URL path 提取 key，格式: /bucket/key"""
        return self.path.lstrip("/")

    def do_GET(self):
        key = self._get_key()
        obj = self.store.get(key)
        if obj is None:
            self.send_error(404, "Not Found")
            return
        data, meta = obj
        self.send_response(200)
        self.send_header("Content-Type", "binary/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("ETag", f'"{meta["etag"]}"')
        self.end_headers()
        self.wfile.write(data)

    def do_PUT(self):
        query = parse_qs(urlparse(self.path).query)
        key = self._get_key()
        content_length = int(self.headers.get("Content-Length", 0))
        data = self.rfile.read(content_length) if content_length > 0 else b""

        # 多部分上传的分片 (partNumber=N&uploadId=X)
        if "partNumber" in query and "uploadId" in query:
            upload_id = query["uploadId"][0]
            part_num = query["partNumber"][0]
            part_key = f"__part_{upload_id}_{part_num}"
            etag = self.store.put(part_key, data)
            # 同时注册到 multipart state
            self.multipart.upload_part(upload_id, data)
            self.send_response(200)
            self.send_header("ETag", f'"{etag}"')
            self.end_headers()
            return

        # 普通 PUT
        etag = self.store.put(key, data)
        self.send_response(200)
        self.send_header("ETag", f'"{etag}"')
        self.end_headers()

    def do_DELETE(self):
        query = parse_qs(urlparse(self.path).query)
        key = self._get_key()

        # Abort multipart upload
        if "uploadId" in query:
            self.multipart.abort(query["uploadId"][0], self.store)
            self.send_response(204)
            self.end_headers()
            return

        # 普通 DELETE
        self.store.delete(key)
        self.send_response(204)
        self.end_headers()

    def do_POST(self):
        query = parse_qs(urlparse(self.path).query)
        key = self._get_key()

        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length) if content_length > 0 else b""

        # Initiate Multipart Upload (?uploads)
        if "uploads" in query and "uploadId" not in query:
            upload_id = self.multipart.initiate(key)
            bucket = key.split("/", 1)[0] if "/" in key else "test"
            xml = (
                '<?xml version="1.0" encoding="UTF-8"?>\n'
                '<InitiateMultipartUploadResult>\n'
                f'  <Bucket>{bucket}</Bucket>\n'
                f'  <Key>{key}</Key>\n'
                f'  <UploadId>{upload_id}</UploadId>\n'
                '</InitiateMultipartUploadResult>'
            )
            self._send_xml(200, xml)
            return

        # Complete Multipart Upload (?uploadId=X)
        if "uploadId" in query and "uploads" not in query:
            upload_id = query["uploadId"][0]
            etag = self.multipart.complete(upload_id, self.store)
            if etag is None:
                self.send_error(404, "NoSuchUpload")
                return
            bucket = key.split("/", 1)[0] if "/" in key else "test"
            xml = (
                '<?xml version="1.0" encoding="UTF-8"?>\n'
                '<CompleteMultipartUploadResult>\n'
                f'  <Location>http://localhost:19000/{key}</Location>\n'
                f'  <Bucket>{bucket}</Bucket>\n'
                f'  <Key>{key}</Key>\n'
                f'  <ETag>"{etag}"</ETag>\n'
                '</CompleteMultipartUploadResult>'
            )
            self._send_xml(200, xml)
            return

        self.send_error(400, "Bad Request")

    def log_message(self, format, *args):
        if os.environ.get("MOCK_VERBOSE"):
            super().log_message(format, *args)


# ==============================================================================
# Server management
# ==============================================================================
def gen_self_signed_cert():
    """如果没有提供证书，自动生成自签名证书"""
    import subprocess
    cert_path = "/tmp/myblob_mock_cert.pem"
    key_path = "/tmp/myblob_mock_key.pem"
    if not os.path.exists(cert_path) or not os.path.exists(key_path):
        subprocess.run([
            "openssl", "req", "-x509", "-newkey", "rsa:2048",
            "-keyout", key_path, "-out", cert_path,
            "-days", "365", "-nodes", "-subj", "/CN=localhost"
        ], check=True, capture_output=True)
    return cert_path, key_path


def run_server(port: int, handler_class, use_tls: bool = False,
               cert_file: str = "", key_file: str = ""):
    """在独立线程中启动 server"""
    server = http.server.ThreadingHTTPServer(
        ("127.0.0.1", port), handler_class
    )
    if use_tls:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(cert_file, key_file)
        # 不验证客户端证书
        ctx.verify_mode = ssl.CERT_NONE
        server.socket = ctx.wrap_socket(server.socket, server_side=True)

    proto = "https" if use_tls else "http"
    print(f"[MOCK] {proto}://127.0.0.1:{port} started ({handler_class.__name__})")
    server.serve_forever()


def main():
    parser = argparse.ArgumentParser(description="MyBlob Mock Server")
    parser.add_argument("--http-port", type=int, default=18888)
    parser.add_argument("--s3-port", type=int, default=19000)
    parser.add_argument("--https-port", type=int, default=18443)
    parser.add_argument("--cert", type=str, default="",
                        help="TLS certificate file (auto-generated if omitted)")
    parser.add_argument("--key", type=str, default="",
                        help="TLS private key file (auto-generated if omitted)")
    args = parser.parse_args()

    # 证书生成
    cert_file = args.cert
    key_file = args.key
    if args.https_port > 0:
        if not cert_file or not key_file:
            cert_file, key_file = gen_self_signed_cert()

    threads = []

    if args.http_port > 0:
        t = threading.Thread(
            target=run_server,
            args=(args.http_port, MockHTTPHandler),
            daemon=True
        )
        t.start()
        threads.append(t)

    if args.s3_port > 0:
        t = threading.Thread(
            target=run_server,
            args=(args.s3_port, S3MockHandler),
            daemon=True
        )
        t.start()
        threads.append(t)

    if args.https_port > 0:
        time.sleep(0.5)  # 等前两个 server 启动
        t = threading.Thread(
            target=run_server,
            args=(args.https_port, MockHTTPHandler, True, cert_file, key_file),
            daemon=True
        )
        t.start()
        threads.append(t)

    print(f"\n[MOCK] All servers ready."
          f" HTTP=:{args.http_port} S3=:{args.s3_port} HTTPS=:{args.https_port}\n"
          f"[MOCK] Press Ctrl+C to stop.\n")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n[MOCK] Shutting down...")
        sys.exit(0)


if __name__ == "__main__":
    main()
