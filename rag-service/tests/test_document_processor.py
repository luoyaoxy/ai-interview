import asyncio
import json
from zipfile import ZipFile

from app.document.processor import LocalDocumentProcessor


def test_docx_extracts_paragraphs(tmp_path) -> None:
    document = tmp_path / "resume.docx"
    document_xml = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:body>
    <w:p><w:r><w:t>C++ engineer</w:t></w:r></w:p>
    <w:p><w:r><w:t>Built a low-latency service.</w:t></w:r></w:p>
  </w:body>
</w:document>"""
    with ZipFile(document, "w") as archive:
        archive.writestr("word/document.xml", document_xml)
    processor = LocalDocumentProcessor(chunk_size=100, chunk_overlap=10)

    parsed = asyncio.run(processor.process(document))

    assert parsed.chunks[0].content == "C++ engineer\nBuilt a low-latency service."
    assert parsed.chunks[0].metadata["format"] == "docx"


def test_text_chunking_preserves_overlap_and_sentence_boundaries(tmp_path) -> None:
    document = tmp_path / "notes.txt"
    document.write_text("第一句。第二句很长，需要被切分。第三句结束。", encoding="utf-8")
    processor = LocalDocumentProcessor(chunk_size=12, chunk_overlap=3)

    parsed = asyncio.run(processor.process(document))

    assert len(parsed.chunks) >= 2
    assert parsed.chunks[0].metadata["source"] == "notes.txt"
    assert parsed.chunks[1].metadata["char_start"] < parsed.chunks[0].metadata["char_end"]


def test_markdown_and_json_keep_structured_metadata(tmp_path) -> None:
    markdown = tmp_path / "guide.md"
    markdown.write_text("# 虚函数\n通过 virtual 声明。\n## 重写\n使用 override。", encoding="utf-8")
    json_file = tmp_path / "qa.json"
    json_file.write_text(
        json.dumps([{"question": "什么是 RAII？", "answer": "资源获取即初始化。"}]),
        encoding="utf-8",
    )
    processor = LocalDocumentProcessor(chunk_size=100, chunk_overlap=10)

    markdown_result = asyncio.run(processor.process(markdown))
    json_result = asyncio.run(processor.process(json_file))

    assert [chunk.metadata["heading"] for chunk in markdown_result.chunks] == [
        "虚函数",
        "重写",
    ]
    assert json_result.chunks[0].metadata["format"] == "json_qa"
    assert json_result.chunks[0].content.startswith("问：什么是 RAII？")
