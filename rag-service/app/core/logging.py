"""Application logging with request correlation and optional JSON output."""

import json
import logging
import sys
from contextvars import ContextVar, Token
from datetime import UTC, datetime
from typing import Any

from app.core.config import Settings

_request_id: ContextVar[str] = ContextVar("request_id", default="-")


def bind_request_id(request_id: str) -> Token[str]:
    """Bind a request ID to logs emitted while processing one request."""

    return _request_id.set(request_id)


def reset_request_id(token: Token[str]) -> None:
    _request_id.reset(token)


class RequestContextFilter(logging.Filter):
    def filter(self, record: logging.LogRecord) -> bool:
        record.request_id = getattr(record, "request_id", _request_id.get())
        return True


class JsonFormatter(logging.Formatter):
    """Format one event per line for container log collectors."""

    _extra_fields = (
        "method",
        "path",
        "status_code",
        "duration_ms",
        "client_ip",
        "error_code",
    )

    def __init__(self, service: str, version: str) -> None:
        super().__init__()
        self.service = service
        self.version = version

    def format(self, record: logging.LogRecord) -> str:
        event: dict[str, Any] = {
            "time": datetime.now(UTC).isoformat(),
            "level": record.levelname,
            "service": self.service,
            "version": self.version,
            "logger": record.name,
            "request_id": getattr(record, "request_id", "-"),
            "message": record.getMessage(),
        }
        for field in self._extra_fields:
            value = getattr(record, field, None)
            if value is not None:
                event[field] = value
        if record.exc_info:
            event["exception"] = self.formatException(record.exc_info)
        return json.dumps(event, ensure_ascii=False)


def configure_logging(settings: Settings) -> None:
    """Apply RAG_LOG_LEVEL and RAG_LOG_FORMAT to the root logger."""

    level = getattr(logging, settings.log_level.upper(), logging.INFO)
    handler = logging.StreamHandler(sys.stdout)
    handler.addFilter(RequestContextFilter())
    if settings.log_format == "json":
        handler.setFormatter(JsonFormatter(settings.service_name, settings.service_version))
    else:
        handler.setFormatter(
            logging.Formatter(
                "%(asctime)s %(levelname)s %(name)s "
                "request_id=%(request_id)s %(message)s"
            )
        )
    logging.basicConfig(level=level, handlers=[handler], force=True)
