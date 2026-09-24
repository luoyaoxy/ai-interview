"""PDF, DOCX, Markdown, text, and JSON parsing with overlapping chunks."""

import asyncio
import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path
from zipfile import BadZipFile, ZipFile

from pypdf import PdfReader

from app.core.errors import ServiceError
from app.document.ports import DocumentChunk, ParsedDocument

_SENTENCE_BOUNDARIES = frozenset("\n.!?。！？、")
_MARKDOWN_HEADING = re.compile(r"^(#{1,6})\s+(.+?)\s*$")


class LocalDocumentProcessor:
    """Python equivalent of the current C++ DocumentLoader/PDFParser path."""

    def __init__(self, chunk_size: int, chunk_overlap: int) -> None:
        if chunk_overlap >= chunk_size:
            raise ValueError("chunk_overlap must be smaller than chunk_size")
        self._chunk_size = chunk_size
        self._chunk_overlap = chunk_overlap

    async def process(self, file_path: Path) -> ParsedDocument:
        return await asyncio.to_thread(self._process_sync, file_path)

    def _process_sync(self, file_path: Path) -> ParsedDocument:
        extension = file_path.suffix.lower()
        if extension == ".pdf":
            chunks = self._parse_pdf(file_path)
        elif extension == ".docx":
            chunks = self._parse_docx(file_path)
        elif extension == ".md":
            chunks = self._parse_markdown(self._read_text(file_path), file_path.name)
        elif extension == ".txt":
            chunks = self._chunk_text(self._read_text(file_path), file_path.name)
        elif extension == ".json":
            chunks = self._parse_json(self._read_text(file_path), file_path.name)
        else:
            raise ServiceError(
                status_code=415,
                code="UNSUPPORTED_DOCUMENT_TYPE",
                message=f"Unsupported document extension '{extension}'",
            )

        if not chunks:
            raise ServiceError(
                status_code=400,
                code="EMPTY_DOCUMENT",
                message="No usable text could be extracted from the document",
            )
        return ParsedDocument(file_name=file_path.name, chunks=chunks)

    @staticmethod
    def _read_text(file_path: Path) -> str:
        try:
            return file_path.read_text(encoding="utf-8-sig").strip()
        except UnicodeDecodeError as exc:
            raise ServiceError(
                status_code=400,
                code="INVALID_TEXT_ENCODING",
                message="Text documents must use UTF-8 encoding",
            ) from exc

    def _parse_pdf(self, file_path: Path) -> list[DocumentChunk]:
        try:
            reader = PdfReader(file_path)
            chunks: list[DocumentChunk] = []
            sequence = 0
            total_pages = len(reader.pages)
            for page_number, page in enumerate(reader.pages, start=1):
                page_text = (page.extract_text() or "").strip()
                page_chunks = self._chunk_text(
                    page_text,
                    file_path.name,
                    start_sequence=sequence,
                    extra_metadata={"page": page_number, "total_pages": total_pages},
                )
                chunks.extend(page_chunks)
                sequence += len(page_chunks)
            return chunks
        except ServiceError:
            raise
        except Exception as exc:
            raise ServiceError(
                status_code=400,
                code="PDF_PARSE_FAILED",
                message=f"Failed to parse PDF document: {exc}",
            ) from exc

    def _parse_docx(self, file_path: Path) -> list[DocumentChunk]:
        try:
            with ZipFile(file_path) as archive:
                document_xml = archive.read("word/document.xml")
            root = ET.fromstring(document_xml)
            namespace = {"w": "http://schemas.openxmlformats.org/wordprocessingml/2006/main"}
            paragraphs = []
            for paragraph in root.findall(".//w:p", namespace):
                text = "".join(
                    node.text or ""
                    for node in paragraph.findall(".//w:t", namespace)
                ).strip()
                if text:
                    paragraphs.append(text)
            return self._chunk_text(
                "\n".join(paragraphs),
                file_path.name,
                extra_metadata={"format": "docx"},
            )
        except (BadZipFile, KeyError, ET.ParseError) as exc:
            raise ServiceError(
                status_code=400,
                code="DOCX_PARSE_FAILED",
                message=f"Failed to parse DOCX document: {exc}",
            ) from exc

    def _parse_markdown(self, text: str, source_name: str) -> list[DocumentChunk]:
        sections: list[tuple[str, str]] = []
        heading = ""
        buffer: list[str] = []

        def flush() -> None:
            content = "\n".join(buffer).strip()
            if content:
                sections.append((heading, content))

        for line in text.splitlines():
            match = _MARKDOWN_HEADING.match(line)
            if match:
                flush()
                buffer = []
                heading = match.group(2).strip()
            else:
                buffer.append(line)
        flush()

        chunks: list[DocumentChunk] = []
        sequence = 0
        for section_heading, section_text in sections:
            section_chunks = self._chunk_text(
                section_text,
                source_name,
                start_sequence=sequence,
                extra_metadata={
                    "format": "markdown_section",
                    "heading": section_heading,
                },
            )
            chunks.extend(section_chunks)
            sequence += len(section_chunks)
        return chunks

    @staticmethod
    def _parse_json(text: str, source_name: str) -> list[DocumentChunk]:
        try:
            value = json.loads(text)
        except json.JSONDecodeError as exc:
            raise ServiceError(
                status_code=400,
                code="JSON_PARSE_FAILED",
                message=f"Failed to parse JSON document: {exc.msg}",
            ) from exc

        if isinstance(value, list):
            chunks: list[DocumentChunk] = []
            for index, item in enumerate(value):
                if isinstance(item, dict):
                    question = item.get("question", item.get("q", ""))
                    answer = item.get("answer", item.get("a", ""))
                    content = (
                        f"问：{question}\n答：{answer}"
                        if question and answer
                        else json.dumps(item, ensure_ascii=False)
                    )
                else:
                    content = json.dumps(item, ensure_ascii=False)
                chunks.append(
                    DocumentChunk(
                        id=f"temporary-{index}",
                        content=content,
                        sequence=index,
                        metadata={
                            "source": source_name,
                            "chunk_index": index,
                            "format": "json_qa",
                        },
                    )
                )
            return chunks

        if isinstance(value, dict):
            return [
                DocumentChunk(
                    id="temporary-0",
                    content=json.dumps(value, ensure_ascii=False, indent=2),
                    sequence=0,
                    metadata={
                        "source": source_name,
                        "chunk_index": 0,
                        "format": "json_object",
                    },
                )
            ]
        return []

    def _chunk_text(
        self,
        text: str,
        source_name: str,
        *,
        start_sequence: int = 0,
        extra_metadata: dict[str, object] | None = None,
    ) -> list[DocumentChunk]:
        text = text.strip()
        if not text:
            return []

        chunks: list[DocumentChunk] = []
        start = 0
        sequence = start_sequence
        while start < len(text):
            end = min(start + self._chunk_size, len(text))
            if end < len(text):
                search_from = start + int(self._chunk_size * 0.4)
                boundary = max(
                    (
                        index + 1
                        for index in range(search_from, end)
                        if text[index] in _SENTENCE_BOUNDARIES
                    ),
                    default=-1,
                )
                if boundary > start + self._chunk_size // 3:
                    end = boundary

            content = text[start:end]
            metadata: dict[str, object] = {
                "source": source_name,
                "chunk_index": sequence,
                "char_start": start,
                "char_end": end,
                "char_count": end - start,
            }
            if extra_metadata:
                metadata.update(extra_metadata)
            chunks.append(
                DocumentChunk(
                    id=f"temporary-{sequence}",
                    content=content,
                    sequence=sequence,
                    metadata=metadata,
                )
            )
            sequence += 1
            if end >= len(text):
                break
            next_start = max(0, end - self._chunk_overlap)
            start = next_start if next_start > start else start + 1
        return chunks
