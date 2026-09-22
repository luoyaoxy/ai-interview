"""Configurable vector and lexical retrieval orchestration."""

import asyncio

from app.embedding.ports import EmbeddingProvider
from app.retrieval.lexical import lexical_score
from app.retrieval.ports import RetrievedSource
from app.vector_store.sqlite_store import SqliteVectorStore


class SemanticRetriever:
    """Keep the existing name while supporting semantic and hybrid modes."""
    def __init__(
        self,
        embedding: EmbeddingProvider,
        store: SqliteVectorStore,
        *,
        mode: str = "hybrid",
        candidate_k: int = 20,
        lexical_threshold: float = 0.3,
    ) -> None:
        self._embedding = embedding
        self._store = store
        self._mode = mode
        self._candidate_k = candidate_k
        self._lexical_threshold = lexical_threshold

    async def search(
        self,
        question: str,
        knowledge_base_id: str,
        *,
        top_k: int,
        similarity_threshold: float,
    ) -> list[RetrievedSource]:
        vectors = await self._embedding.embed([question])
        candidate_k = max(top_k, self._candidate_k)
        semantic = await asyncio.to_thread(
            self._store.search,
            knowledge_base_id,
            vectors[0],
            top_k=candidate_k if self._mode == "hybrid" else top_k,
            similarity_threshold=similarity_threshold,
        )
        if self._mode == "hybrid":
            keywords = await asyncio.to_thread(
                self._store.search_keywords, knowledge_base_id, question, top_k=candidate_k
            )
            keywords = [
                match for match in keywords if match.score >= self._lexical_threshold
            ]
            # Reciprocal rank fusion preserves candidates found by either method.
            candidates = {match.chunk.id: match for match in semantic}
            candidates.update((match.chunk.id, match) for match in keywords)
            ranks: dict[str, float] = {}
            for matches in (semantic, keywords):
                for rank, match in enumerate(matches, start=1):
                    if match.chunk.id in candidates:
                        ranks[match.chunk.id] = ranks.get(match.chunk.id, 0.0) + 1 / (60 + rank)
            # Rerank the fused set using query coverage. This is deliberately
            # local and deterministic; a model-based reranker can replace it.
            ordered = sorted(
                candidates.values(),
                key=lambda match: (
                    -(0.65 * min(ranks[match.chunk.id] * 30.5, 1.0)
                      + 0.35 * lexical_score(question, match.chunk.content)),
                    match.chunk.id,
                ),
            )[:top_k]
            matches = [
                (match, min(1.0, 0.65 * min(ranks[match.chunk.id] * 30.5, 1.0)
                 + 0.35 * lexical_score(question, match.chunk.content)))
                for match in ordered
            ]
        else:
            matches = [(match, match.score) for match in semantic]
        return [
            RetrievedSource(
                document_id=match.chunk.document_id,
                document_name=match.chunk.document_name,
                chunk_id=match.chunk.id,
                content=match.chunk.content,
                score=score,
                page=match.chunk.metadata.get("page"),
            )
            for match, score in matches
        ]
