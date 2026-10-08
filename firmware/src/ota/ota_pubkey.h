// Public half of the firmware signing key (ECDSA P-256). The private half is
// kept outside the repository; scripts/ota_release.sh signs with it. Replacing
// this key means every tracker has to be flashed once over USB.
#pragma once

constexpr const char* kOtaPublicKeyPem =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEGIj55xhxhx0SA/x6DaiufUbnAJUj\n"
    "IcrMD98d1ulyWZFRv5YKlFC4DSvPt7tuE0h/CbDInDdVoGztb7pRHDggUg==\n"
    "-----END PUBLIC KEY-----\n";
