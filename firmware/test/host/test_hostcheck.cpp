// Host-side check of isLocalBrokerHost(), run in CI:
//   g++ -std=c++17 -Wall -I src test/host/test_hostcheck.cpp -o /tmp/t && /tmp/t
// A public name accepted here would let the firmware send the MQTT password in
// plain text (2026-10-08), so the cases below lean on near misses.
#include <cstdio>

#include "util/hostcheck.h"

int main() {
  struct Case {
    const char* host;
    bool local;
  } cases[] = {
      {"10.0.40.19", true},          {"192.168.18.19", true},
      {"172.16.0.1", true},          {"172.31.255.1", true},
      {"127.0.0.1", true},           {"mqtt.local", true},
      {"broker.LAN", true},          {"x.home.arpa", true},
      {"172.32.0.1", false},         {"172.15.0.1", false},
      {"8.8.8.8", false},            {"car-mqtt.example.com", false},
      {"local", false},              {"evil.local.example.com", false},
      {"10.0.0.1.example.com", false}, {"192.168.1.1x", false},
      {"", false},
  };
  int failed = 0;
  for (const Case& c : cases) {
    if (transport::isLocalBrokerHost(c.host) != c.local) {
      std::printf("FAIL: %s should be %s\n", c.host, c.local ? "local" : "public");
      ++failed;
    }
  }
  std::printf("%s, %zu cases\n", failed ? "FAILED" : "ok", sizeof(cases) / sizeof(cases[0]));
  return failed ? 1 : 0;
}
