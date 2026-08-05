"""Question embedding and vector search orchestration."""

import asyncio

from app.embedding.ports import EmbeddingProvider
from app.retrieval.ports import RetrievedSource
from app.vector_store.sqlite_store import SqliteVectorStore


class SemanticRetriever:
    def __init__(self, embedding: EmbeddingProvider, store: SqliteVectorStore) -> None:
        self._embedding = embedding
        self._store = store

    async def search(
        self,
        question: str,
        knowledge_base_id: str,
        *,
        top_k: int,
        similarity_threshold: float,
    ) -> list[RetrievedSource]:
        vectors = await self._embedding.embed([question])
        matches = await asyncio.to_thread(
            self._store.search,
            knowledge_base_id,
            vectors[0],
            top_k=top_k,
            similarity_threshold=similarity_threshold,
        )
        return [
            RetrievedSource(
                document_id=match.chunk.document_id,
                document_name=match.chunk.document_name,
                chunk_id=match.chunk.id,
                content=match.chunk.content,
                score=match.score,
                page=match.chunk.metadata.get("page"),
            )
            for match in matches
        ]
