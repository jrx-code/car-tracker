"""Patch TinyGSM 0.12's A7672X plain TCP connect before the build.

TinyGsmA7672X::modemConnect sends AT+CTCPKA first and AT+NETOPEN second, and
gives up on the first ERROR. On the A7670E-FASE (firmware A7670M7_V1.11.1,
checked over AT on 2026-10-08) that order can never succeed:

  - AT+CTCPKA answers ERROR until the network is open, so the first connect
    fails before NETOPEN is ever sent;
  - AT+NETOPEN on an already open network answers "+IP ERROR: Network is
    already opened" / ERROR, so every later connect fails as well.

The patch opens the network only when AT+NETOPEN? says it is closed, waits for
the +NETOPEN: 0 URC, and sends CTCPKA afterwards.

modemSend has the same problem on the plain socket: after AT+CIPSEND the
module confirms with "+CIPSEND: <mux>,<requested>,<sent>", but TinyGSM waits
10 s for "+CCHSEND: 0,0", which only the modem's own SSL socket reports, and
then returns 0. The bytes did go out, so BearSSL resent its ClientHello on the
same connection and the broker closed it with bad_record_mac (packet capture,
2026-10-08). The patch reads the +CIPSEND confirmation instead. TinyGSM master is unchanged
since v0.12.0 (May 2024), so there is nothing upstream to bump to.

The script is idempotent, and it stops the build when the code it replaces is
not found, so a TinyGSM upgrade cannot silently drop the fix.
"""

import os

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

# Bump the number when the patch changes; a copy patched by an older version
# then fails the build and has to be reinstalled (delete .pio/libdeps/<env>/TinyGSM).
MARK = "car-tracker patch 2: A7672X plain socket"

OLD_KEEPALIVE = """    // +CTCPKA:<keepalive>,<keepidle>,<keepcount>,<keepinterval>
    sendAT(GF("+CTCPKA=1,2,5,1"));
    if (waitResponse(2000L) != 1) { return false; }
"""

NEW_KEEPALIVE = "    // " + MARK + """ (see firmware/scripts/patch_tinygsm_a7672x.py).
    // CTCPKA answers ERROR until the network is open, and NETOPEN answers
    // ERROR once it is, so open it only when closed and set keepalive after.
    if (!ssl) {
      sendAT(GF("+NETOPEN?"));
      int8_t net = waitResponse(2000L, GF("+NETOPEN: 1"), GF("+NETOPEN: 0"));
      if (net == 0) { return false; }
      waitResponse();
      if (net != 1) {
        sendAT(GF("+NETOPEN"));
        if (waitResponse(2000L) != 1) { return false; }
        if (waitResponse(20000L, GF("+NETOPEN: 0")) != 1) { return false; }
      }
    }
    // +CTCPKA:<keepalive>,<keepidle>,<keepcount>,<keepinterval>
    sendAT(GF("+CTCPKA=1,2,5,1"));
    if (waitResponse(2000L) != 1 && ssl) { return false; }
"""

OLD_NETOPEN = """      sendAT(GF("+NETOPEN"));
      if (waitResponse(2000L) != 1) { return false; }

      sendAT(GF("+NETOPEN?"));
      if (waitResponse(2000L) != 1) { return false; }

      sendAT(GF("+CIPOPEN="), 0,"""

NEW_NETOPEN = "      // NETOPEN moved above, before CTCPKA (" + MARK + """).
      sendAT(GF("+CIPOPEN="), 0,"""

OLD_SEND = """    if (waitResponse() != 1) { return 0; }
    if (waitResponse(10000L, GF("+CCHSEND: 0,0" AT_NL),
"""

NEW_SEND = "    if (waitResponse() != 1) { return 0; }\n    // " + MARK + """: a plain socket
    // confirms with +CIPSEND: <mux>,<requested>,<sent>, never with +CCHSEND.
    if (!hasSSL) {
      if (waitResponse(10000L, GF("+CIPSEND:")) != 1) { return 0; }
      streamSkipUntil(',');  // mux
      streamSkipUntil(',');  // requested length
      int16_t sent = streamGetIntBefore('\\n');
      return sent > 0 ? sent : 0;
    }
    if (waitResponse(10000L, GF("+CCHSEND: 0,0" AT_NL),
"""


def patch():
    path = os.path.join(
        env.subst("$PROJECT_LIBDEPS_DIR"),  # noqa: F821
        env.subst("$PIOENV"),  # noqa: F821
        "TinyGSM", "src", "TinyGsmClientA7672x.h",
    )
    if not os.path.isfile(path):
        # Envs without TinyGSM (wifi_dev, lilygo_wifi) have nothing to patch.
        if "TinyGSM" in str(env.GetProjectOption("lib_deps", "")):  # noqa: F821
            raise SystemExit(f"patch_tinygsm_a7672x: {path} not found")
        return
    with open(path, encoding="utf-8") as f:
        src = f.read()
    if MARK in src:
        return
    if "car-tracker patch" in src:
        raise SystemExit(
            f"patch_tinygsm_a7672x: {path} carries an older patch; "
            "delete the TinyGSM directory under .pio/libdeps and rebuild")
    for old in (OLD_KEEPALIVE, OLD_NETOPEN, OLD_SEND):
        if src.count(old) != 1:
            raise SystemExit(
                f"patch_tinygsm_a7672x: expected code not found in {path}; "
                "TinyGSM changed, review the patch")
    src = (src.replace(OLD_KEEPALIVE, NEW_KEEPALIVE)
              .replace(OLD_NETOPEN, NEW_NETOPEN)
              .replace(OLD_SEND, NEW_SEND))
    with open(path, "w", encoding="utf-8") as f:
        f.write(src)
    print("patch_tinygsm_a7672x: patched", path)


patch()
