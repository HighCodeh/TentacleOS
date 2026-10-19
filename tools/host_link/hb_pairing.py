"""Host-link v2: PSK-authenticated ephemeral X25519 and physical confirmation."""
import hashlib
import hmac
import os
import struct
import time

MAGIC = b"HB"
ENVELOPE_VERSION = 1
PAIR_VERSION = 2
TYPE_HELLO = 0x10
TYPE_ACK = 0x11
TYPE_FINISH = 0x12
TYPE_READY = 0x13
HEADER_SIZE = 10
MAC_SIZE = 16
HELLO_PREFIX_SIZE = 49
ACK_PREFIX_SIZE = 55
MAX_BODY = 4096 - HEADER_SIZE - MAC_SIZE


def mac16(key, message):
    return hmac.new(key, message, hashlib.sha256).digest()[:MAC_SIZE]


def derive(secret, psk, label, transcript):
    prk = hmac.new(psk, secret, hashlib.sha256).digest()
    return hmac.new(prk, label + transcript + b"\x01", hashlib.sha256).digest()


def verify_frame(frame, key, previous=None):
    if len(frame) < HEADER_SIZE + 3 + MAC_SIZE or frame[:4] != MAGIC + bytes([ENVELOPE_VERSION, 1]):
        raise PermissionError("invalid authenticated host-link envelope")
    counter, length = struct.unpack_from("<IH", frame, 4)
    if length < 3 or length > MAX_BODY or len(frame) != HEADER_SIZE + length + MAC_SIZE:
        raise PermissionError("invalid host-link frame length")
    if not hmac.compare_digest(frame[-MAC_SIZE:], mac16(key, frame[2:-MAC_SIZE])):
        raise PermissionError("host-link MAC mismatch")
    if previous is not None and counter <= previous:
        raise PermissionError("replayed host-link response")
    return counter


class Handshake:
    def __init__(self, psk):
        if len(psk) != 32:
            raise ValueError("PSK must contain 32 bytes")
        try:
            from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
            from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
        except ImportError as error:
            raise RuntimeError("install tools/host_link/requirements.txt for X25519 support") from error
        self.psk = psk
        self._private = X25519PrivateKey.generate()
        public = self._private.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
        self.prefix = bytes([PAIR_VERSION]) + os.urandom(16) + public
        self.hello = self.prefix + mac16(psk, b"tos-host-v2-client" + self.prefix)

    def accept(self, ack):
        if len(ack) != ACK_PREFIX_SIZE + MAC_SIZE or ack[0] != PAIR_VERSION:
            raise PermissionError("device requires the X25519 v2 pairing protocol")
        transcript = self.prefix + ack[:ACK_PREFIX_SIZE]
        if not hmac.compare_digest(ack[ACK_PREFIX_SIZE:], mac16(self.psk, b"tos-host-v2-server" + transcript)):
            raise PermissionError("device PSK/transcript proof mismatch")
        from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PublicKey
        shared = self._private.exchange(X25519PublicKey.from_public_bytes(ack[17:49]))
        self._private = None
        self.k_a2d = derive(shared, self.psk, b"tos-host-v2-a2d", transcript)
        self.k_d2a = derive(shared, self.psk, b"tos-host-v2-d2a", transcript)
        code = derive(shared, self.psk, b"tos-host-v2-code", transcript)
        self.code = struct.unpack_from("<I", code)[0] % 1000000
        return ack[49:55]


def perform_handshake(link, psk):
    link.k_a2d = link.k_d2a = None
    link.rx_counter = None
    link.tx_counter = 0
    exchange = Handshake(psk)
    link._send(TYPE_HELLO, 0, 0, exchange.hello, authed=False)
    deadline = time.monotonic() + 5.0
    while True:
        frame = link._read_frame(deadline)
        if frame is None:
            raise TimeoutError("no HELLO_ACK; open the Companion pairing screen on the device")
        length = struct.unpack_from("<H", frame, 8)[0]
        body = frame[HEADER_SIZE:HEADER_SIZE + length]
        if body[:3] == bytes([TYPE_ACK, 0, 0]) and frame[:4] == MAGIC + bytes([ENVELOPE_VERSION, 0]):
            device_id = exchange.accept(body[3:])
            break
    link.k_a2d, link.k_d2a = exchange.k_a2d, exchange.k_d2a
    print(f"Compare code {exchange.code:06d} on the device, then press its OK button.")
    deadline = time.monotonic() + 30.0
    try:
        while time.monotonic() < deadline:
            link.tx_counter = 1
            link._send(TYPE_FINISH, 0, 0, b"", authed=True)
            try:
                frame = link._read_frame(min(deadline, time.monotonic() + 0.5))
            except TimeoutError:
                continue
            if frame is None:
                continue
            length = struct.unpack_from("<H", frame, 8)[0]
            body = frame[HEADER_SIZE:HEADER_SIZE + length]
            if body == bytes([TYPE_READY, 0, 0]):
                verify_frame(frame, link.k_d2a)
                link.tx_counter = 2
                return device_id.hex(":")
        raise TimeoutError("pairing was not confirmed on the device before expiry")
    except BaseException:
        link.k_a2d = link.k_d2a = None
        raise
