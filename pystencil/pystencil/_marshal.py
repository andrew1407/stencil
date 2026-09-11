"""ctypes marshalling for the stencil_cli_* ABI: str/bytes/bytearray -> C views.

Memory model (matches the C side): the caller owns every buffer; core never allocates,
frees, or returns heap memory. C strings are const char* UTF-8 inputs. RGBA8 buffers are
interleaved R,G,B,A bytes. An in-place op mutates a Python bytearray directly through the
aliasing view :func:`_buf_view` builds.
"""

from __future__ import annotations

import ctypes


def _encode(s: str) -> bytes:
    """Encode a Python str to the UTF-8 bytes the C ABI expects for const char*."""
    return s.encode("utf-8")


def _buf_view(buf: bytearray) -> ctypes.Array:
    """Cast a bytearray into a ctypes c_uint8 array that aliases the SAME memory.

    Using from_buffer (not from_buffer_copy) is what makes in-place core ops visible to
    the caller's bytearray.
    """
    return (ctypes.c_uint8 * len(buf)).from_buffer(buf)


def _bytes_arg(src: bytes) -> ctypes.Array:
    """A read-only c_uint8 array for an immutable bytes source (a private copy is fine)."""
    return (ctypes.c_uint8 * len(src)).from_buffer_copy(src)


def _check_pixels(buf, pixel_count: int, name: str) -> None:
    """Guard a flat RGBA8 op: the declared pixel count must be non-negative and fit
    within `buf`. The C kernels only reject non-positive dims, so an oversized
    pixel_count would read/write past the buffer — a memory-safety hole. Raise
    ValueError here before the ctypes call rather than corrupt memory."""
    if pixel_count < 0:
        raise ValueError(f"{name}: pixel_count must be non-negative, got {pixel_count}")
    if pixel_count * 4 > len(buf):
        raise ValueError(
            f"{name}: pixel_count {pixel_count} needs {pixel_count * 4} bytes but "
            f"buffer holds {len(buf)}"
        )


def _check_dims(buf, w: int, h: int, name: str) -> None:
    """Guard a width x height RGBA8 op against oversized/negative dims vs `buf`."""
    if w < 0 or h < 0:
        raise ValueError(f"{name}: dimensions must be non-negative, got {w}x{h}")
    if w * h * 4 > len(buf):
        raise ValueError(
            f"{name}: {w}x{h} needs {w * h * 4} bytes but buffer holds {len(buf)}"
        )
