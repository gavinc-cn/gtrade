#!/bin/bash

set -e

CERT_DIR="$(cd "$(dirname "$0")" && pwd)"
DAYS=3650  # 10年有效期

cd "$CERT_DIR"

echo "🔐 Generating mTLS Certificates for GTrade Desktop Gateway"
echo "=========================================================="

echo ""
echo "📝 Step 1: Generating CA certificate..."
# 1. 生成CA私钥
openssl genrsa -out ca.key 4096

# 2. 生成CA证书
openssl req -new -x509 -days $DAYS -key ca.key -out ca.crt \
    -subj "/C=CN/ST=Shanghai/L=Shanghai/O=GTrade/OU=IT/CN=GTrade CA"

echo "✅ CA certificate generated"

echo ""
echo "🖥️  Step 2: Generating server certificate..."
# 3. 生成服务器私钥
openssl genrsa -out server.key 4096

# 4. 创建SAN配置文件
cat > server_san.cnf <<EOF
[req]
default_bits = 4096
prompt = no
default_md = sha256
distinguished_name = dn
req_extensions = v3_req

[dn]
C = CN
ST = Shanghai
L = Shanghai
O = GTrade
OU = IT
CN = localhost

[v3_req]
subjectAltName = @alt_names

[alt_names]
DNS.1 = localhost
DNS.2 = *.localhost
IP.1 = 127.0.0.1
IP.2 = 127.0.0.1
IP.3 = 0.0.0.0
EOF

# 5. 生成服务器证书签名请求（带SAN）
openssl req -new -key server.key -out server.csr -config server_san.cnf

# 6. 用CA签名服务器证书（带SAN扩展）
openssl x509 -req -days $DAYS -in server.csr -CA ca.crt -CAkey ca.key \
    -CAcreateserial -out server.crt -extensions v3_req -extfile server_san.cnf

echo "✅ Server certificate generated (with SAN for localhost, 127.0.0.1)"

echo ""
echo "💻 Step 3: Generating client certificate..."
# 6. 生成客户端私钥
openssl genrsa -out client.key 4096

# 7. 生成客户端证书签名请求
openssl req -new -key client.key -out client.csr \
    -subj "/C=CN/ST=Shanghai/L=Shanghai/O=GTrade/OU=IT/CN=gtrade-client"

# 8. 用CA签名客户端证书
openssl x509 -req -days $DAYS -in client.csr -CA ca.crt -CAkey ca.key \
    -CAcreateserial -out client.crt

echo "✅ Client certificate generated"

echo ""
echo "🧹 Cleaning up temporary files..."
rm -f server.csr client.csr ca.srl server_san.cnf

echo ""
echo "🔒 Setting file permissions..."
chmod 600 *.key
chmod 644 *.crt

echo ""
echo "✅ Certificate generation complete!"
echo ""
echo "📁 Generated files:"
ls -lh *.crt *.key

echo ""
echo "📋 Certificate information:"
echo "=========================="
echo ""
echo "🔐 CA Certificate:"
openssl x509 -in ca.crt -noout -subject -dates
echo ""
echo "🖥️  Server Certificate:"
openssl x509 -in server.crt -noout -subject -dates
echo ""
echo "💻 Client Certificate:"
openssl x509 -in client.crt -noout -subject -dates

echo ""
echo "🎉 All certificates are ready to use!"
echo ""
echo "Next steps:"
echo "  1. Copy ca.crt, server.crt, server.key to backend config directory"
echo "  2. Copy ca.crt, client.crt, client.key to Qt client config directory"
echo "  3. Update config.yml files with certificate paths"
