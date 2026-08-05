"""Service health endpoint."""

from datetime import UTC, datetime
from typing import Annotated

from fastapi import APIRouter, Depends

from app.api.schemas import (
    DependencyStatus,
    HealthDependencies,
    HealthResponse,
    ServiceStatus,
)
from app.core.config import Settings, get_settings
from app.services import ServiceContainer, get_services

router = APIRouter(tags=["Health"])


@router.get("/health", operation_id="getHealth", response_model=HealthResponse)
def get_health(
    settings: Annotated[Settings, Depends(get_settings)],
    services: Annotated[ServiceContainer, Depends(get_services)],
) -> HealthResponse:
    """Report configuration readiness for migrated dependencies."""

    embedding_status = (
        DependencyStatus.HEALTHY
        if settings.embedding_api_url and settings.embedding_model
        else DependencyStatus.UNHEALTHY
    )
    llm_status = (
        DependencyStatus.HEALTHY
        if settings.llm_api_url and settings.llm_api_key and settings.llm_model
        else DependencyStatus.UNHEALTHY
    )
    dependencies = HealthDependencies(
        embedding=embedding_status,
        vector_store=(DependencyStatus.HEALTHY if services.store else DependencyStatus.UNHEALTHY),
        llm=llm_status,
    )
    dependency_values = (
        dependencies.embedding,
        dependencies.vector_store,
        dependencies.llm,
    )
    service_status = (
        ServiceStatus.HEALTHY
        if all(status == DependencyStatus.HEALTHY for status in dependency_values)
        else ServiceStatus.DEGRADED
    )

    return HealthResponse(
        status=service_status,
        service=settings.service_name,
        version=settings.service_version,
        time=datetime.now(UTC),
        dependencies=dependencies,
    )
