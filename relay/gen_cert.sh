#!/usr/bin/env bash
# Copyright (c) 2026 LNXSeus. All Rights Reserved.
#
# This project is proprietary software. You are granted a license to use the software as-is.
# You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
# or its source code in any way without the express written permission of the copyright holder.
#
# Generate the self-signed cert the relay server uses.
#
# The Advancely client pins the SHA-256 fingerprint of this cert.
# Ten-year validity so rotations are rare.
#
# TODO: CERT ROTATION REMINDER
# When this cert expires (10 years from generation) or is compromised:
#   1. Regenerate cert on the relay box with this script.
#   2. Print the new fingerprint:  openssl x509 -in cert.pem -noout -fingerprint -sha256
#   3. Update the pinned fingerprint in Advancely source (search for RELAY_CERT_FINGERPRINT).
#   4. Ship an Advancely release with the new pin BEFORE swapping the cert on the server,
#      otherwise old clients will fail to connect.
#
# Cert generated:  <25.04.26>
# Expires:         <23.04.36>

set -euo pipefail

# Pass the relay's public IP as the first argument:
#   ./gen_cert.sh <RELAY_IP>
# Run this on the relay server itself, not in any committed config.
if [ $# -lt 1 ]; then
    echo "usage: $0 <RELAY_IP>" >&2
    exit 1
fi
RELAY_IP="$1"

openssl req -x509 -newkey rsa:4096 -nodes \
    -keyout key.pem \
    -out cert.pem \
    -days 3650 \
    -subj "/CN=advancely-relay" \
    -addext "subjectAltName=IP:${RELAY_IP}"

chmod 600 key.pem
echo
echo "=== PIN THIS FINGERPRINT IN THE ADVANCELY CLIENT ==="
openssl x509 -in cert.pem -noout -fingerprint -sha256