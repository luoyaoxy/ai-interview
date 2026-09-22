"""Small, dependency-free lexical scores for Chinese and English snippets."""

import re

_WORDS = re.compile(r"[a-z0-9_][a-z0-9_+#.]*|[\u3400-\u9fff]+", re.IGNORECASE)


def terms(text: str) -> set[str]:
    result: set[str] = set()
    for match in _WORDS.finditer(text.lower()):
        word = match.group()
        if "\u3400" <= word[0] <= "\u9fff" and len(word) > 1:
            result.update(word[index : index + 2] for index in range(len(word) - 1))
        else:
            result.add(word)
    return result


def lexical_score(question: str, content: str) -> float:
    query_terms = terms(question)
    if not query_terms:
        return 0.0
    overlap = len(query_terms & terms(content)) / len(query_terms)
    return overlap
