"""FastAPI application entry point."""

import logging
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager

from fastapi import FastAPI, Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse

from app import __version__
from app.api.health import router as health_router
from app.api.router import api_router
from app.api.schemas import ErrorBody, ErrorResponse
from app.core.config import get_settings
from app.core.errors import ServiceError
from app.core.logging import configure_logging
from app.core.middleware import request_id_middleware
from app.services import get_services

logger = logging.getLogger(__name__)


def _request_id(request: Request) -> str:
    return getattr(request.state, "request_id", "unknown")


def _serializable_validation_errors(exc: RequestValidationError) -> list[dict[str, object]]:
    """Convert Pydantic error contexts, which may contain Exception objects, to JSON."""

    errors: list[dict[str, object]] = []
    for raw_error in exc.errors():
        error = dict(raw_error)
        context = error.get("ctx")
        if isinstance(context, dict):
            error["ctx"] = {key: str(value) for key, value in context.items()}
        errors.append(error)
    return errors


@asynccontextmanager
async def lifespan(_application: FastAPI) -> AsyncIterator[None]:
    yield
    if get_services.cache_info().currsize:
        get_services().close()
        get_services.cache_clear()


def create_app() -> FastAPI:
    """Build the application without creating infrastructure at import time."""

    settings = get_settings()
    configure_logging(settings)
    application = FastAPI(
        title="AI Interview RAG Service API",
        version=settings.service_version or __version__,
        description="Standalone RAG service for the AI interview application.",
        lifespan=lifespan,
    )
    application.middleware("http")(request_id_middleware)
    application.include_router(health_router)
    application.include_router(api_router)

    @application.exception_handler(ServiceError)
    async def service_error_handler(request: Request, exc: ServiceError) -> JSONResponse:
        logger.warning(
            "service_error",
            extra={"error_code": exc.code, "request_id": _request_id(request)},
        )
        payload = ErrorResponse(
            request_id=_request_id(request),
            error=ErrorBody(code=exc.code, message=exc.message, details=exc.details),
        )
        return JSONResponse(
            status_code=exc.status_code,
            content=payload.model_dump(mode="json"),
            headers={"X-Request-ID": _request_id(request)},
        )

    @application.exception_handler(RequestValidationError)
    async def validation_error_handler(
        request: Request,
        exc: RequestValidationError,
    ) -> JSONResponse:
        payload = ErrorResponse(
            request_id=_request_id(request),
            error=ErrorBody(
                code="VALIDATION_ERROR",
                message="Request validation failed",
                details={"errors": _serializable_validation_errors(exc)},
            ),
        )
        return JSONResponse(
            status_code=400,
            content=payload.model_dump(mode="json"),
            headers={"X-Request-ID": _request_id(request)},
        )

    @application.exception_handler(Exception)
    async def unexpected_error_handler(request: Request, exc: Exception) -> JSONResponse:
        logger.exception(
            "unhandled_request_error",
            exc_info=exc,
            extra={"error_code": "INTERNAL_ERROR", "request_id": _request_id(request)},
        )
        payload = ErrorResponse(
            request_id=_request_id(request),
            error=ErrorBody(
                code="INTERNAL_ERROR",
                message="The RAG service encountered an unexpected error",
            ),
        )
        return JSONResponse(
            status_code=500,
            content=payload.model_dump(mode="json"),
            headers={"X-Request-ID": _request_id(request)},
        )

    return application


app = create_app()
