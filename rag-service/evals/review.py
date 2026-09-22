"""Summarize human answer reviews recorded in an evaluation report."""

import argparse
import json
from pathlib import Path


def summarize(rows: list[dict]) -> dict[str, dict[str, float | int | None]]:
    summary = {}
    for field in ("answer_correct", "claims_supported", "citations_correct"):
        eligible = [row for row in rows if row["answerable"]] if field != "answer_correct" else rows
        reviewed = [
            row["human_review"][field]
            for row in eligible
            if row.get("human_review", {}).get(field) in (True, False)
        ]
        summary[field] = {
            "reviewed": len(reviewed),
            "total": len(eligible),
            "pass_rate": sum(reviewed) / len(reviewed) if reviewed else None,
        }
    return summary


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    report = json.loads(args.report.read_text(encoding="utf-8"))
    if not all("human_review" in row for row in report["rows"]):
        parser.error("Run evals.run with --answers before reviewing answers")
    print(json.dumps(summarize(report["rows"]), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
