"""Ports for question embedding and source retrieval."""

from dataclasses import dataclass
from typing import Protocol


@dataclass(frozen=True, slots=True)
class RetrievedSource:
    document_id: str
    document_name: str
    chunk_id: str
    content: str
    score: float
    page: int | None = None


class Retriever(Protocol):
    """Retrieve ranked sources for a natural-language question."""

    async def search(
        self,
        question: str,
        knowledge_base_id: str,
        *,
        top_k: int,
        similarity_threshold: float,
    ) -> list[RetrievedSource]: ...
