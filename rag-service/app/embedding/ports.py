"""Ports implemented by an Embedding model adapter."""

from typing import Protocol


class EmbeddingProvider(Protocol):
    """Convert a batch of texts into vectors with a stable dimension."""

    async def embed(self, texts: list[str]) -> list[list[float]]: ...

    async def dimension(self) -> int: ...
