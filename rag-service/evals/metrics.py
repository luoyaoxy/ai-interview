"""Metrics over human-annotated evidence quotes, independent of chunk IDs."""

import re
from collections.abc import Sequence


def normalize(value: str) -> str:
    return re.sub(r"\s+", "", value).lower()


def evidence_hits(
    evidence: Sequence[dict[str, str]], sources: Sequence[dict[str, object]]
) -> list[int | None]:
    """Return the one-based rank of each gold quote, if present in a source."""
    return [
        next(
            (
                rank
                for rank, source in enumerate(sources, start=1)
                if source["document_name"] == item["document"]
                and normalize(item["quote"]) in normalize(str(source["content"]))
            ),
            None,
        )
        for item in evidence
    ]


def retrieval_metrics(rows: Sequence[dict[str, object]], ks: tuple[int, ...] = (1, 3, 5)) -> dict:
    positive = [row for row in rows if row["answerable"]]
    negative = [row for row in rows if not row["answerable"]]
    result: dict[str, object] = {
        "answerable_count": len(positive),
        "unanswerable_count": len(negative),
    }
    for k in ks:
        result[f"hit@{k}"] = (
            sum(any(rank is not None and rank <= k for rank in row["evidence_ranks"])
                for row in positive) / len(positive) if positive else None
        )
        result[f"recall@{k}"] = (
            sum(sum(rank is not None and rank <= k for rank in row["evidence_ranks"])
                / len(row["evidence_ranks"]) for row in positive) / len(positive)
            if positive else None
        )
    result["mrr"] = (
        sum(1 / min(rank for rank in row["evidence_ranks"] if rank is not None)
            if any(rank is not None for rank in row["evidence_ranks"]) else 0
            for row in positive) / len(positive) if positive else None
    )
    result["no_answer_empty_retrieval_rate"] = (
        sum(not row["sources"] for row in negative) / len(negative) if negative else None
    )
    return result


def answer_checks(row: dict[str, object]) -> dict[str, object]:
    """Structural checks only; semantic support still requires human review."""
    answer = str(row["answer"])
    citations = [int(value) for value in re.findall(r"\[(\d+)\]", answer)]
    sources = row["answer_sources"]
    valid = bool(citations) and all(1 <= index <= len(sources) for index in citations)
    gold = row["evidence"]
    cited_gold = valid and all(
        any(
            source["document_name"] == item["document"]
            and normalize(item["quote"]) in normalize(str(source["content"]))
            for item in gold
        )
        for source in (sources[index - 1] for index in citations)
    )
    return {"citation_indices_valid": valid, "cited_sources_match_gold": cited_gold}
