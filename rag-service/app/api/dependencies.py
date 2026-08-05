"""Shared FastAPI dependencies."""

import secrets
from typing import Annotated

from fastapi import Depends
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer

from app.core.config import Settings, get_settings
from app.core.errors import ServiceError

bearer_scheme = HTTPBearer(auto_error=False, scheme_name="bearerAuth")


def require_bearer_token(
    credentials: Annotated[HTTPAuthorizationCredentials | None, Depends(bearer_scheme)],
    settings: Annotated[Settings, Depends(get_settings)],
) -> str:
    """Validate the service-to-client bearer token."""

    if credentials is None or credentials.scheme.lower() != "bearer":
        raise ServiceError(
            status_code=401,
            code="UNAUTHORIZED",
            message="Missing bearer token",
        )
    if not secrets.compare_digest(credentials.credentials, settings.api_key):
        raise ServiceError(
            status_code=401,
            code="UNAUTHORIZED",
            message="Invalid bearer token",
        )
    return credentials.credentials


Authorized = Annotated[str, Depends(require_bearer_token)]
