import asyncio
import json
from pathlib import Path

from app.retrieval.lexical import lexical_score
from app.retrieval.service import SemanticRetriever
from app.vector_store.ports import SearchMatch, StoredChunk
from evals.metrics import answer_checks, evidence_hits, retrieval_metrics
from evals.review import summarize
from evals.run import validate_dataset


def test_shipped_evidence_is_present_in_source_documents() -> None:
    dataset = json.loads((Path(__file__).parents[1] / "evals" / "dataset.json").read_text(
        encoding="utf-8"
    ))
    validate_dataset(dataset)
    assert len(dataset["questions"]) >= 10
    assert any(not row["answerable"] for row in dataset["questions"])


def test_evidence_metrics_count_each_gold_quote_and_ignore_chunk_ids() -> None:
    sources = [
        {"document_name": "a.md", "content": "first fact", "chunk_id": "random-1"},
        {"document_name": "a.md", "content": "second fact", "chunk_id": "random-2"},
    ]
    ranks = evidence_hits(
        [{"document": "a.md", "quote": "first fact"},
         {"document": "a.md", "quote": "second fact"}],
        sources,
    )
    result = retrieval_metrics(
        [{"answerable": True, "evidence_ranks": ranks, "sources": sources},
         {"answerable": False, "evidence_ranks": [], "sources": []}]
    )
    assert result["hit@1"] == 1
    assert result["recall@1"] == 0.5
    assert result["recall@3"] == 1
    assert result["no_answer_empty_retrieval_rate"] == 1


def test_lexical_scoring_handles_chinese_and_cpp_terms() -> None:
    assert lexical_score("虚函数如何绑定", "虚函数通过动态绑定实现多态") > 0
    assert lexical_score("std::unique_lock 能延迟加锁吗", "std::unique_lock 可以延迟加锁") > 0.3
    assert lexical_score("Python GIL", "vector 扩容使迭代器失效") == 0


def test_hybrid_can_find_keyword_candidate_below_vector_threshold() -> None:
    class Embedding:
        async def embed(self, _texts):
            return [[1.0, 0.0]]

    class Store:
        def search(self, _kb, _vector, *, top_k, similarity_threshold):
            return []

        def search_keywords(self, _kb, _question, *, top_k):
            return [SearchMatch(
                chunk=StoredChunk("c1", "kb1", "d1", "source.md", "vector 扩容会使迭代器失效", []),
                score=0.7,
            )]

    retriever = SemanticRetriever(Embedding(), Store())
    sources = asyncio.run(
        retriever.search("vector 扩容迭代器", "kb1", top_k=3, similarity_threshold=0.7)
    )
    assert [source.chunk_id for source in sources] == ["c1"]


def test_citation_check_requires_current_source_and_gold_evidence() -> None:
    row = {
        "answer": "答案。[1]",
        "answer_sources": [{"document_name": "a.md", "content": "gold fact"}],
        "evidence": [{"document": "a.md", "quote": "gold fact"}],
    }
    assert answer_checks(row) == {
        "citation_indices_valid": True,
        "cited_sources_match_gold": True,
    }
    row["answer"] = "答案。[2]"
    assert answer_checks(row)["citation_indices_valid"] is False


def test_keyword_search_stays_in_the_selected_knowledge_base(services) -> None:
    first = services.store.create_knowledge_base("first", "", "general_assistant")
    second = services.store.create_knowledge_base("second", "", "general_assistant")
    for kb, content in ((first, "vector 扩容使迭代器失效"), (second, "vector 扩容很快")):
        document = services.store.create_document(kb.id, "sample.md", "sample.md")
        services.store.upsert_chunks([
            StoredChunk(
                f"chunk-{kb.id}", kb.id, document.id, "sample.md", content, [1.0, 0.0, 0.0]
            )
        ])
    matches = services.store.search_keywords(first.id, "vector 扩容", top_k=10)
    assert len(matches) == 1
    assert matches[0].chunk.knowledge_base_id == first.id


def test_answer_without_current_source_citation_is_rejected(services) -> None:
    kb = services.store.create_knowledge_base("citation-test", "", "general_assistant")
    document = services.store.create_document(kb.id, "sample.md", "sample.md")
    services.store.upsert_chunks([
        StoredChunk("chunk-1", kb.id, document.id, "sample.md", "虚函数实现多态", [1.0, 0.0, 0.0])
    ])

    async def uncited(_messages):
        return "虚函数实现多态。"

    services.llm.generate = uncited
    result = asyncio.run(services.rag.query(
        question="虚函数", knowledge_base_id=kb.id, conversation_id=None, role_type=None
    ))
    assert result.knowledge_found is False
    assert result.sources == []
    assert services.conversations.messages(result.conversation_id) == []


def test_review_summary_counts_only_explicit_human_labels() -> None:
    rows = [
        {"answerable": True, "human_review": {
            "answer_correct": True, "claims_supported": False, "citations_correct": True,
        }},
        {"answerable": False, "human_review": {
            "answer_correct": None, "claims_supported": None, "citations_correct": None,
        }},
    ]
    summary = summarize(rows)
    assert summary["answer_correct"] == {"reviewed": 1, "total": 2, "pass_rate": 1.0}
    assert summary["claims_supported"] == {"reviewed": 1, "total": 1, "pass_rate": 0.0}
