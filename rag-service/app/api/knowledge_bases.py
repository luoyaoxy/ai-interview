"""Knowledge-base HTTP routes."""

from pathlib import Path
from typing import Annotated

from fastapi import APIRouter, Depends, Query, Request, Response, status

from app.api.dependencies import Authorized
from app.api.mappers import to_knowledge_base
from app.api.schemas import (
    CreateKnowledgeBaseRequest,
    KnowledgeBasePage,
    KnowledgeBaseResponse,
    UpdateKnowledgeBaseRequest,
)
from app.core.pagination import decode_cursor, encode_cursor
from app.services import ServiceContainer, get_services

router = APIRouter(prefix="/knowledge-bases", tags=["KnowledgeBases"])
Services = Annotated[ServiceContainer, Depends(get_services)]


@router.post(
    "",
    operation_id="createKnowledgeBase",
    response_model=KnowledgeBaseResponse,
    status_code=status.HTTP_201_CREATED,
)
def create_knowledge_base(
    request: CreateKnowledgeBaseRequest,
    _authorization: Authorized,
    services: Services,
    http_request: Request,
) -> KnowledgeBaseResponse:
    record = services.store.create_knowledge_base(
        request.name,
        request.description,
        request.role_type.value,
    )
    return KnowledgeBaseResponse(
        request_id=http_request.state.request_id,
        knowledge_base=to_knowledge_base(record),
    )


@router.get("", operation_id="listKnowledgeBases", response_model=KnowledgeBasePage)
def list_knowledge_bases(
    _authorization: Authorized,
    services: Services,
    http_request: Request,
    page_size: Annotated[int, Query(ge=1, le=100)] = 20,
    cursor: Annotated[str | None, Query(max_length=512)] = None,
) -> KnowledgeBasePage:
    offset = decode_cursor(cursor)
    records, total = services.store.list_knowledge_bases(offset, page_size)
    next_offset = offset + len(records)
    return KnowledgeBasePage(
        request_id=http_request.state.request_id,
        items=[to_knowledge_base(record) for record in records],
        next_cursor=encode_cursor(next_offset) if next_offset < total else None,
        total=total,
    )


@router.get(
    "/{knowledge_base_id}",
    operation_id="getKnowledgeBase",
    response_model=KnowledgeBaseResponse,
)
def get_knowledge_base(
    knowledge_base_id: str,
    _authorization: Authorized,
    services: Services,
    http_request: Request,
) -> KnowledgeBaseResponse:
    return KnowledgeBaseResponse(
        request_id=http_request.state.request_id,
        knowledge_base=to_knowledge_base(services.store.get_knowledge_base(knowledge_base_id)),
    )


@router.patch(
    "/{knowledge_base_id}",
    operation_id="updateKnowledgeBase",
    response_model=KnowledgeBaseResponse,
)
def update_knowledge_base(
    knowledge_base_id: str,
    request: UpdateKnowledgeBaseRequest,
    _authorization: Authorized,
    services: Services,
    http_request: Request,
) -> KnowledgeBaseResponse:
    record = services.store.update_knowledge_base(
        knowledge_base_id,
        name=request.name,
        description=request.description,
        role_type=request.role_type.value if request.role_type else None,
    )
    return KnowledgeBaseResponse(
        request_id=http_request.state.request_id,
        knowledge_base=to_knowledge_base(record),
    )


@router.delete(
    "/{knowledge_base_id}",
    operation_id="deleteKnowledgeBase",
    status_code=status.HTTP_204_NO_CONTENT,
)
def delete_knowledge_base(
    knowledge_base_id: str,
    _authorization: Authorized,
    services: Services,
) -> Response:
    storage_paths = services.store.delete_knowledge_base(knowledge_base_id)
    for storage_path in storage_paths:
        Path(storage_path).unlink(missing_ok=True)
    return Response(status_code=status.HTTP_204_NO_CONTENT)
