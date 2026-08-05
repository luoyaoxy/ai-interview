"""Knowledge-base document HTTP routes."""

from pathlib import Path
from typing import Annotated
from uuid import uuid4

from fastapi import (
    APIRouter,
    BackgroundTasks,
    Depends,
    File,
    Form,
    Query,
    Request,
    Response,
    UploadFile,
    status,
)

from app.api.dependencies import Authorized
from app.api.mappers import to_document
from app.api.schemas import DocumentPage, DocumentResponse, DocumentStatus
from app.core.errors import ServiceError
from app.core.pagination import decode_cursor, encode_cursor
from app.services import ServiceContainer, get_services

router = APIRouter(
    prefix="/knowledge-bases/{knowledge_base_id}/documents",
    tags=["Documents"],
)
Services = Annotated[ServiceContainer, Depends(get_services)]


@router.post(
    "",
    operation_id="uploadDocument",
    response_model=DocumentResponse,
    status_code=status.HTTP_202_ACCEPTED,
)
async def upload_document(
    knowledge_base_id: str,
    _authorization: Authorized,
    services: Services,
    http_request: Request,
    background_tasks: BackgroundTasks,
    file: Annotated[UploadFile, File()],
    display_name: Annotated[str | None, Form(min_length=1, max_length=255)] = None,
) -> DocumentResponse:
    services.store.get_knowledge_base(knowledge_base_id)
    original_name = Path(file.filename or "").name
    if not original_name:
        raise ServiceError(
            status_code=400,
            code="MISSING_FILE_NAME",
            message="Uploaded document must have a file name",
        )
    extension = Path(original_name).suffix.lower()
    if extension not in services.settings.supported_extensions:
        raise ServiceError(
            status_code=415,
            code="UNSUPPORTED_DOCUMENT_TYPE",
            message=f"Unsupported document extension '{extension}'",
        )

    visible_name = Path(display_name).name if display_name else original_name
    destination_dir = services.settings.upload_dir / knowledge_base_id
    destination_dir.mkdir(parents=True, exist_ok=True)
    destination = destination_dir / f"{uuid4()}-{original_name}"
    total_bytes = 0
    try:
        with destination.open("wb") as output:
            while content := await file.read(1024 * 1024):
                total_bytes += len(content)
                if total_bytes > services.settings.max_upload_bytes:
                    raise ServiceError(
                        status_code=413,
                        code="DOCUMENT_TOO_LARGE",
                        message="Uploaded document exceeds the configured size limit",
                    )
                output.write(content)
        record = services.store.create_document(
            knowledge_base_id,
            visible_name,
            str(destination.resolve()),
        )
    except Exception:
        destination.unlink(missing_ok=True)
        raise
    finally:
        await file.close()

    background_tasks.add_task(services.ingestion.process, record.id)
    return DocumentResponse(
        request_id=http_request.state.request_id,
        document=to_document(record),
    )


@router.get("", operation_id="listDocuments", response_model=DocumentPage)
def list_documents(
    knowledge_base_id: str,
    _authorization: Authorized,
    services: Services,
    http_request: Request,
    page_size: Annotated[int, Query(ge=1, le=100)] = 20,
    cursor: Annotated[str | None, Query(max_length=512)] = None,
    document_status: Annotated[DocumentStatus | None, Query(alias="status")] = None,
) -> DocumentPage:
    offset = decode_cursor(cursor)
    records, total = services.store.list_documents(
        knowledge_base_id,
        offset,
        page_size,
        document_status.value if document_status else None,
    )
    next_offset = offset + len(records)
    return DocumentPage(
        request_id=http_request.state.request_id,
        items=[to_document(record) for record in records],
        next_cursor=encode_cursor(next_offset) if next_offset < total else None,
        total=total,
    )


@router.get(
    "/{document_id}",
    operation_id="getDocument",
    response_model=DocumentResponse,
)
def get_document(
    knowledge_base_id: str,
    document_id: str,
    _authorization: Authorized,
    services: Services,
    http_request: Request,
) -> DocumentResponse:
    record = services.store.get_document(knowledge_base_id, document_id)
    return DocumentResponse(
        request_id=http_request.state.request_id,
        document=to_document(record),
    )


@router.delete(
    "/{document_id}",
    operation_id="deleteDocument",
    status_code=status.HTTP_204_NO_CONTENT,
)
def delete_document(
    knowledge_base_id: str,
    document_id: str,
    _authorization: Authorized,
    services: Services,
) -> Response:
    storage_path = services.store.delete_document(knowledge_base_id, document_id)
    Path(storage_path).unlink(missing_ok=True)
    return Response(status_code=status.HTTP_204_NO_CONTENT)
