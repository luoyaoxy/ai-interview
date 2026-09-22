# RAG quality evaluation

`dataset.json` is a small, hand-annotated starting set. Each answerable question
has a reference answer and one or more exact quotes from the uploaded documents.
The three unanswerable questions check whether the service refuses when the
documents do not support an answer. Expand this set with real interview material
before treating its scores as a release gate.

## Run a baseline and an experiment

Start the RAG service with a working embedding provider. To collect the original
vector-only baseline, set `RAG_RETRIEVAL_MODE=semantic` and restart the service.
From `rag-service/`, set `$env:RAG_API_KEY` to the value used by the service
(the `.env` file does not automatically set it in your shell), then run:

```powershell
python -m evals.run --api-key $env:RAG_API_KEY --label semantic --report evals/reports/semantic.json
```

The command uploads the fixed corpus into a new knowledge base. It prints the
knowledge base ID. To compare retrieval methods on the **same indexed corpus**,
restart the service with `RAG_RETRIEVAL_MODE=hybrid` and run:

```powershell
python -m evals.run --api-key $env:RAG_API_KEY --knowledge-base-id "kb-id-from-first-run" --label hybrid --report evals/reports/hybrid.json
```

Add `--answers` to evaluate the generation path as well. This calls the
configured LLM and may incur model costs. Use a separate report file. Pass the
key through a private environment variable as shown above.

The report records each question, ranked sources, evidence ranks, reference
answer, latency, and (with `--answers`) the answer and citation checks. Reports
belong in `evals/reports/` and are ignored by Git. Keep the corpus ID in both
reports to confirm that the comparison used the same documents and embeddings.

## What the numbers mean

- `Hit@K`: fraction of answerable questions with at least one annotated quote
  among the first K retrieved chunks.
- `Recall@K`: mean fraction of each question's annotated quotes found in the
  first K chunks. A question needing two quotes contributes `0.5` if one is found.
- `MRR`: mean reciprocal rank of the first matching quote.
- `no_answer_empty_retrieval_rate`: fraction of unanswerable questions with no
  retrieved chunks. A nonempty retrieval alone does not prove the answer is
  supported.
- `refusal_rate`: fraction of unanswerable questions for which `/rag/query`
  returned `knowledge_found=false`.
- Citation checks verify that the answer cites an existing current source and
  whether cited chunks contain annotated quotes. They do **not** decide whether
  each generated claim is entailed by those quotes.

For every generated answer, fill the `human_review` fields in a copy of the
report: `answer_correct`, `claims_supported`, and `citations_correct`. Review
the reference answer, the answer text, cited chunks, and the original quote.
Set each field to `true` or `false`; keep `null` until reviewed. Recheck all
failures and a sample of passing answers after changing models or prompts.
After review, summarize the labels with `python -m evals.review
evals/reports/answers-reviewed.json`. The output includes coverage so a small
reviewed subset cannot be mistaken for a complete answer quality score.

The service uses Chinese bigrams and English tokens for lexical matching,
fuses vector and lexical candidates, then reranks them by query coverage. Tune
`RAG_RETRIEVAL_CANDIDATE_K`, `RAG_LEXICAL_THRESHOLD`, `RAG_SIMILARITY_THRESHOLD`,
and `RAG_TOP_K` only against a development subset. Keep a separate held-out
subset for final comparison. The current lexical scan has linear cost in the
number of chunks; profile latency before using a large knowledge base.

In hybrid mode, returned `score` is a fused ranking score, not cosine
similarity. `similarity_threshold` filters the vector candidates; keyword
candidates use `RAG_LEXICAL_THRESHOLD`. Compare ranks and evidence hits across
modes rather than comparing their raw scores.
