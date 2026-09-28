"""Domain models for interview sessions."""

from dataclasses import dataclass, field
from datetime import UTC, datetime
from enum import StrEnum


class InterviewStatus(StrEnum):
    CREATED = "created"
    IN_PROGRESS = "in_progress"
    COMPLETED = "completed"


@dataclass(slots=True)
class AnswerEvaluation:
    score: float
    feedback: str
    follow_up_needed: bool = False
    follow_up_question: str | None = None


@dataclass(slots=True)
class AnswerRecord:
    question: str
    answer: str
    evaluation: AnswerEvaluation
    is_follow_up: bool = False


@dataclass(slots=True)
class InterviewReport:
    overall_score: float
    summary: str
    strengths: list[str] = field(default_factory=list)
    improvements: list[str] = field(default_factory=list)
    recommendation: str = ""


@dataclass(slots=True)
class InterviewSession:
    id: str
    candidate_name: str
    position: str
    question_count: int
    status: InterviewStatus = InterviewStatus.CREATED
    resume_text: str = ""
    resume_file_name: str | None = None
    questions: list[str] = field(default_factory=list)
    current_question_index: int = 0
    pending_follow_up: str | None = None
    answers: list[AnswerRecord] = field(default_factory=list)
    report: InterviewReport | None = None
    created_at: datetime = field(default_factory=lambda: datetime.now(UTC))
    updated_at: datetime = field(default_factory=lambda: datetime.now(UTC))
