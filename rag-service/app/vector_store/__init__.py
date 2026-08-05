"""Knowledge-base metadata and vector persistence capability."""

from app.vector_store.ports import (
    DocumentRecord,
    KnowledgeBaseRecord,
    SearchMatch,
    StoredChunk,
    VectorStore,
)

__all__ = [
    "DocumentRecord",
    "KnowledgeBaseRecord",
    "SearchMatch",
    "StoredChunk",
    "VectorStore",
]
