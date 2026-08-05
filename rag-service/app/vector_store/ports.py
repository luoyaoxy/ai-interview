"""Storage ports independent of SQLite, FAISS, or another backend."""

from dataclasses import dataclass, field
from datetime import datetime
from typing import Any, Protocol


@dataclass(frozen=True, slots=True)
class StoredChunk:
    id: str
    knowledge_base_id: str
    document_id: str
    document_name: str
    content: str
    vector: list[float]
    metadata: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True, slots=True)
class KnowledgeBaseRecord:
    id: str
    name: str
    description: str
    role_type: str
    document_count: int
    chunk_count: int
    created_at: datetime
    updated_at: datetime


@dataclass(frozen=True, slots=True)
class DocumentRecord:
    id: str
    knowledge_base_id: str
    file_name: str
    storage_path: str
    status: str
    chunk_count: int
    error_message: str
    created_at: datetime
    updated_at: datetime


@dataclass(frozen=True, slots=True)
class SearchMatch:
    chunk: StoredChunk
    score: float


class VectorStore(Protocol):
    """Persist chunks and perform similarity search within one knowledge base."""

    def upsert_chunks(self, chunks: list[StoredChunk]) -> int: ...

    def delete_document(self, knowledge_base_id: str, document_id: str) -> str: ...

    def search(
        self,
        knowledge_base_id: str,
        query_vector: list[float],
        *,
        top_k: int,
        similarity_threshold: float,
    ) -> list[SearchMatch]: ...
