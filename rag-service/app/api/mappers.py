"""Map infrastructure records to stable public API schemas."""

from app.api.schemas import Document, DocumentStatus, KnowledgeBase, RoleType, Source
from app.retrieval.ports import RetrievedSource
from app.vector_store.ports import DocumentRecord, KnowledgeBaseRecord


def to_knowledge_base(record: KnowledgeBaseRecord) -> KnowledgeBase:
    try:
        role_type = RoleType(record.role_type)
    except ValueError:
        role_type = RoleType.DEFAULT
    return KnowledgeBase(
        id=record.id,
        name=record.name,
        description=record.description,
        role_type=role_type,
        document_count=record.document_count,
        chunk_count=record.chunk_count,
        created_at=record.created_at,
        updated_at=record.updated_at,
    )


def to_document(record: DocumentRecord) -> Document:
    return Document(
        id=record.id,
        knowledge_base_id=record.knowledge_base_id,
        file_name=record.file_name,
        status=DocumentStatus(record.status),
        chunk_count=record.chunk_count,
        error_message=record.error_message,
        created_at=record.created_at,
        updated_at=record.updated_at,
    )


def to_source(source: RetrievedSource) -> Source:
    return Source(
        document_id=source.document_id,
        document_name=source.document_name,
        chunk_id=source.chunk_id,
        content=source.content,
        score=source.score,
        page=source.page,
    )
