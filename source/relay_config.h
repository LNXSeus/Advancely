// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.
//
// Created by Linus on 25.04.2026.
//

#ifndef RELAY_CONFIG_H
#define RELAY_CONFIG_H

#define RELAY_HOST "172.245.81.237"

#define RELAY_PORT 5842

// SHA-256 fingerprint of the relay's self-signed certificate.
// Generated 2026-04-25, expires 2036-04-23 (10y validity from gen_cert.sh).
#define RELAY_CERT_FINGERPRINT_SHA256 \
    "21:B3:65:A2:30:A9:C2:EF:91:24:70:9B:AA:41:F9:F6:" \
    "7A:F6:31:F5:03:9D:24:5B:41:D3:37:29:1E:78:C5:BE"

#endif // RELAY_CONFIG_H