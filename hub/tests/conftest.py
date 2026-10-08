"""Test bootstrap.

paho does `import dns.resolver` inside a try/except ImportError to enable SRV
lookups. On some workstations dnspython drags in aioquic -> pyOpenSSL, and a
pyOpenSSL that does not match the installed cryptography raises AttributeError
rather than ImportError, so paho's guard does not catch it and the import of the
whole client explodes.

That is a broken workstation environment, not a problem with this service (the
LXC installs paho from apt and does not need SRV lookups). Blocking the optional
dependency here keeps the tests runnable anywhere: `sys.modules["dns"] = None`
makes `import dns.resolver` raise ImportError, which paho handles by design.
"""

import sys

sys.modules.setdefault("dns", None)
