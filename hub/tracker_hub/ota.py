"""Firmware updates over the MQTT link.

The hub only stages and serves. An admin posts a signed image; the hub stores
it, sends the vehicle an "ota" command carrying size, SHA-256 and signature,
and then answers the device's chunk requests on <prefix>/<id>/ota/req with
<prefix>/<id>/ota/data: a 4-byte little-endian offset followed by the bytes.
The device checks the signature against its built-in key before it asks for
the first chunk, and the hash before it boots the image (firmware ota/ota.h),
so the hub is not trusted with the image's integrity and does not hold a key.
"""

from __future__ import annotations

import base64
import hashlib
import logging
import os
import re
import struct
import threading
import time
from collections.abc import Callable
from typing import Any

_LOG = logging.getLogger(__name__)

# One OTA slot of the ESP32 default partition table (app0/app1, 0x140000).
MAX_IMAGE = 0x140000
MAX_CHUNK = 1536  # matches the firmware: MQTT_MAX_PACKET_SIZE is 2048
DEFAULT_CHUNK = 1024
_VEHICLE = re.compile(r"^[a-z0-9_-]{1,16}$")


class OtaError(ValueError):
    """A request the hub refuses; the message is shown to the admin."""


class OtaServer:
    def __init__(self, directory: str, topic_prefix: str,
                 publish_bytes: Callable[[str, bytes], None],
                 publish_command: Callable[..., bool]) -> None:
        self.directory = directory
        self.topic_prefix = topic_prefix
        self._publish_bytes = publish_bytes
        self._publish_command = publish_command
        self._lock = threading.Lock()
        self._staged: dict[str, dict[str, Any]] = {}

    def _path(self, vehicle_id: str) -> str:
        return os.path.join(self.directory, f"{vehicle_id}.bin")

    def stage(self, vehicle_id: str, image: bytes, signature_b64: str,
              chunk: int = DEFAULT_CHUNK) -> dict[str, Any]:
        """Store the image and tell the vehicle to fetch it."""
        if not _VEHICLE.match(vehicle_id):
            raise OtaError("bad vehicle id")
        if not image or len(image) > MAX_IMAGE:
            raise OtaError(f"image must be 1..{MAX_IMAGE} bytes")
        # ESP32 application images start with the 0xE9 magic byte.
        if image[0] != 0xE9:
            raise OtaError("not an ESP32 application image")
        try:
            signature = base64.b64decode(signature_b64, validate=True)
        except ValueError as exc:
            raise OtaError("signature is not base64") from exc
        if not 8 <= len(signature) <= 80:
            raise OtaError("signature has an implausible length")
        if not 256 <= chunk <= MAX_CHUNK:
            raise OtaError(f"chunk must be 256..{MAX_CHUNK}")

        os.makedirs(self.directory, exist_ok=True)
        tmp = self._path(vehicle_id) + ".tmp"
        with open(tmp, "wb") as f:
            f.write(image)
        os.replace(tmp, self._path(vehicle_id))

        sha = hashlib.sha256(image).hexdigest()
        meta = {
            "size": len(image), "sha256": sha, "chunk": chunk,
            "staged": time.time(), "served": 0, "last_offset": None,
            "last_request": None,
        }
        with self._lock:
            self._staged[vehicle_id] = meta
        sent = self._publish_command(
            vehicle_id, "ota", size=len(image), sha256=sha,
            sig=base64.b64encode(signature).decode(), chunk=chunk,
        )
        _LOG.info("%s: staged %d byte image %s, command %s",
                  vehicle_id, len(image), sha[:12], "sent" if sent else "NOT sent")
        return {**meta, "command_sent": sent}

    def on_request(self, vehicle_id: str, data: dict[str, Any]) -> bool:
        """Answer one chunk request. False when it is refused."""
        with self._lock:
            meta = self._staged.get(vehicle_id)
        if meta is None:
            _LOG.warning("%s: chunk request with nothing staged", vehicle_id)
            return False
        try:
            offset = int(data.get("off", -1))
            length = int(data.get("len", 0))
        except (TypeError, ValueError):
            return False
        if offset < 0 or not 0 < length <= MAX_CHUNK or offset + length > meta["size"]:
            _LOG.warning("%s: bad chunk request off=%s len=%s", vehicle_id, offset, length)
            return False
        with open(self._path(vehicle_id), "rb") as f:
            f.seek(offset)
            chunk = f.read(length)
        if len(chunk) != length:
            return False
        self._publish_bytes(
            f"{self.topic_prefix}/{vehicle_id}/ota/data", struct.pack("<I", offset) + chunk
        )
        with self._lock:
            meta["served"] += 1
            meta["last_offset"] = offset
            meta["last_request"] = time.time()
        return True

    def status(self, vehicle_id: str) -> dict[str, Any] | None:
        with self._lock:
            meta = self._staged.get(vehicle_id)
            if meta is None:
                return None
            done = (meta["last_offset"] or 0) + meta["chunk"] if meta["served"] else 0
            return {**meta, "progress": round(min(1.0, done / meta["size"]), 3)}
