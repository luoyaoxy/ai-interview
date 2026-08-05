"""Opaque cursor helpers used by list endpoints."""

from base64 import urlsafe_b64decode, urlsafe_b64encode

from app.core.errors import ServiceError


def encode_cursor(offset: int) -> str:
    return urlsafe_b64encode(str(offset).encode()).decode().rstrip("=")


def decode_cursor(cursor: str | None) -> int:
    if cursor is None:
        return 0
    try:
        padded = cursor + "=" * (-len(cursor) % 4)
        offset = int(urlsafe_b64decode(padded.encode()).decode())
        if offset < 0:
            raise ValueError
        return offset
    except (ValueError, UnicodeDecodeError) as exc:
        raise ServiceError(
            status_code=400,
            code="INVALID_CURSOR",
            message="Pagination cursor is invalid",
        ) from exc
