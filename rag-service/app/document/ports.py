"""Ports implemented by the future document migration."""

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Protocol


@dataclass(frozen=True, slots=True)
class DocumentChunk:
    id: str
    content: str
    sequence: int
    metadata: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True, slots=True)
class ParsedDocument:
    file_name: str
    chunks: list[DocumentChunk]


class DocumentProcessor(Protocol):
    """Parse and chunk one uploaded knowledge-base document."""

    async def process(self, file_path: Path) -> ParsedDocument: ...
