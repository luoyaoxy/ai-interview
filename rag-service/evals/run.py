"""Run annotated retrieval and optional answer evaluation against a live service."""

import argparse
import json
import time
from pathlib import Path
from uuid import uuid4

import httpx

from evals.metrics import answer_checks, evidence_hits, normalize, retrieval_metrics


def validate_dataset(dataset: dict) -> None:
    documents = {item["name"]: item["content"] for item in dataset["documents"]}
    if len(documents) != len(dataset["documents"]):
        raise ValueError("Document names must be unique")
    ids = [item["id"] for item in dataset["questions"]]
    if len(ids) != len(set(ids)):
        raise ValueError("Question IDs must be unique")
    for question in dataset["questions"]:
        evidence = question["evidence"]
        if bool(evidence) != question["answerable"]:
            raise ValueError(f"{question['id']}: answerable must match evidence")
        for item in evidence:
            if item["document"] not in documents or normalize(item["quote"]) not in normalize(
                documents[item["document"]]
            ):
                raise ValueError(f"{question['id']}: evidence quote is absent from the document")


def create_corpus(client: httpx.Client, dataset: dict) -> str:
    response = client.post(
        "/knowledge-bases",
        json={"name": f"rag-eval-{uuid4().hex[:12]}", "role_type": "general_assistant"},
    )
    response.raise_for_status()
    knowledge_base_id = response.json()["knowledge_base"]["id"]
    for document in dataset["documents"]:
        response = client.post(
            f"/knowledge-bases/{knowledge_base_id}/documents",
            files={"file": (document["name"], document["content"].encode(), "text/markdown")},
        )
        response.raise_for_status()
        document_id = response.json()["document"]["id"]
        deadline = time.monotonic() + 120
        while True:
            status = client.get(
                f"/knowledge-bases/{knowledge_base_id}/documents/{document_id}"
            )
            status.raise_for_status()
            state = status.json()["document"]["status"]
            if state == "ready":
                break
            if state == "failed" or time.monotonic() >= deadline:
                raise RuntimeError(f"Document {document['name']} did not become ready: {state}")
            time.sleep(0.5)
    return knowledge_base_id


def run(args: argparse.Namespace) -> dict:
    dataset = json.loads(args.dataset.read_text(encoding="utf-8"))
    validate_dataset(dataset)
    rows = []
    with httpx.Client(
        base_url=args.base_url.rstrip("/") + "/api/v1",
        headers={"Authorization": f"Bearer {args.api_key}"},
        timeout=60,
    ) as client:
        knowledge_base_id = args.knowledge_base_id or create_corpus(client, dataset)
        for question in dataset["questions"]:
            started = time.perf_counter()
            response = client.post(
                "/rag/search",
                json={
                    "question": question["question"],
                    "knowledge_base_id": knowledge_base_id,
                    "top_k": args.top_k,
                    "similarity_threshold": args.similarity_threshold,
                },
            )
            response.raise_for_status()
            sources = response.json()["sources"]
            row = {
                **question,
                "sources": sources,
                "evidence_ranks": evidence_hits(question["evidence"], sources),
                "search_ms": round((time.perf_counter() - started) * 1000, 2),
            }
            if args.answers:
                started = time.perf_counter()
                response = client.post(
                    "/rag/query",
                    json={
                        "question": question["question"],
                        "knowledge_base_id": knowledge_base_id,
                    },
                )
                response.raise_for_status()
                payload = response.json()
                row.update(
                    answer=payload["answer"],
                    answer_sources=payload["sources"],
                    knowledge_found=payload["knowledge_found"],
                    answer_ms=round((time.perf_counter() - started) * 1000, 2),
                    human_review={
                        "answer_correct": None,
                        "claims_supported": None,
                        "citations_correct": None,
                    },
                )
                row.update(answer_checks(row))
            rows.append(row)
    report = {
        "label": args.label,
        "knowledge_base_id": knowledge_base_id,
        "dataset_version": dataset["version"],
        "top_k": args.top_k,
        "similarity_threshold": args.similarity_threshold,
        "retrieval": retrieval_metrics(rows),
        "rows": rows,
    }
    if args.answers:
        negatives = [row for row in rows if not row["answerable"]]
        positives = [row for row in rows if row["answerable"]]
        report["answers"] = {
            "refusal_rate": sum(not row["knowledge_found"] for row in negatives) / len(negatives)
            if negatives else None,
            "citation_indices_valid_rate": sum(row["citation_indices_valid"] for row in positives)
            / len(positives) if positives else None,
            "cited_sources_match_gold_rate": sum(
                row["cited_sources_match_gold"] for row in positives
            )
            / len(positives) if positives else None,
            "semantic_review_required": True,
        }
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-url", default="http://127.0.0.1:8000")
    parser.add_argument("--api-key", required=True)
    parser.add_argument("--dataset", type=Path, default=Path(__file__).with_name("dataset.json"))
    parser.add_argument("--knowledge-base-id", help="Reuse a previously uploaded evaluation corpus")
    parser.add_argument("--top-k", type=int, default=5)
    parser.add_argument("--similarity-threshold", type=float, default=0.7)
    parser.add_argument(
        "--answers", action="store_true", help="Also run generation and citation checks"
    )
    parser.add_argument("--label", default="experiment")
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    report = run(args)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"Knowledge base: {report['knowledge_base_id']}")
    print(f"Report: {args.report}")
    print(json.dumps(report["retrieval"], ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
