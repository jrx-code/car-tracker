// Plain C++ on purpose (no Arduino headers), so test/host/ can compile it on
// the CI runner: the rule it implements guards the MQTT password.
#pragma once

#include <strings.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace transport {

// True for a host that cannot be on the internet: a literal private or
// loopback IPv4 address, or a name under .local, .lan or .home.arpa.
inline bool isLocalBrokerHost(const char* host) {
  unsigned a = 0, b = 0, c = 0, d = 0;
  char tail = 0;
  if (sscanf(host, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) == 4) {
    return a == 10 || a == 127 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31);
  }
  const size_t n = strlen(host);
  for (const char* suffix : {".local", ".lan", ".home.arpa"}) {
    const size_t m = strlen(suffix);
    if (n > m && strcasecmp(host + n - m, suffix) == 0) return true;
  }
  return false;
}

}  // namespace transport
