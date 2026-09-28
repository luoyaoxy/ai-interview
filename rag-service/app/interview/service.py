"""Interview orchestration, LLM prompts, and durable JSON session storage."""

from __future__ import annotations

import asyncio
import json
import re
from dataclasses import asdict, replace
from datetime import UTC, datetime
from pathlib import Path
from typing import Any
from uuid import uuid4

from app.core.errors import ServiceError
from app.document.processor import LocalDocumentProcessor
from app.interview.models import (
    AnswerEvaluation,
    AnswerRecord,
    InterviewReport,
    InterviewSession,
    InterviewStatus,
)
from app.llm.ports import ChatMessage, LlmProvider

_JSON_FENCE = re.compile(r"```(?:json)?\s*(.*?)```", re.IGNORECASE | re.DOTALL)


class InterviewService:
    """Owns interview state so mobile clients remain thin and credential-free."""

    def __init__(
        self,
        *,
        llm: LlmProvider,
        document_processor: LocalDocumentProcessor,
        store_path: Path,
    ) -> None:
        self._llm = llm
        self._document_processor = document_processor
        self._store_path = store_path
        self._lock = asyncio.Lock()
        self._sessions = self._load()

    async def create(
        self, candidate_name: str, position: str, question_count: int
    ) -> InterviewSession:
        async with self._lock:
            now = datetime.now(UTC)
            session = InterviewSession(
                id=str(uuid4()),
                candidate_name=candidate_name.strip(),
                position=position.strip(),
                question_count=question_count,
                created_at=now,
                updated_at=now,
            )
            self._sessions[session.id] = session
            self._persist()
            return session

    async def attach_resume(
        self, interview_id: str, file_path: Path, file_name: str
    ) -> InterviewSession:
        parsed = await self._document_processor.process(file_path)
        text = "\n\n".join(chunk.content for chunk in parsed.chunks)
        async with self._lock:
            session = self._get(interview_id)
            self._require_status(session, InterviewStatus.CREATED)
            session.resume_text = text[:30000]
            session.resume_file_name = file_name
            self._touch(session)
            self._persist()
            return session

    async def start(self, interview_id: str) -> tuple[InterviewSession, str]:
        async with self._lock:
            session = self._get(interview_id)
            self._require_status(session, InterviewStatus.CREATED)
            raw = await self._llm.generate(
                [
                    ChatMessage(
                        role="system",
                        content=(
                            "你是一名专业技术面试官。只返回 JSON，不要 Markdown。"
                            "格式为 {\"questions\":[\"问题1\"]}。问题应清晰、逐步加深，"
                            "结合岗位和简历，不询问敏感个人信息。"
                        ),
                    ),
                    ChatMessage(
                        role="user",
                        content=(
                            f"候选人：{session.candidate_name}\n岗位：{session.position}\n"
                            f"请生成 {session.question_count} 道主问题。\n"
                            f"简历内容：\n{session.resume_text or '未提供简历'}"
                        ),
                    ),
                ]
            )
            data = self._parse_json(raw, "生成面试题")
            questions = data.get("questions")
            if not isinstance(questions, list):
                self._invalid_llm("面试题响应缺少 questions 数组")
            cleaned = [str(item).strip() for item in questions if str(item).strip()]
            if len(cleaned) < session.question_count:
                self._invalid_llm("大模型返回的面试题数量不足")
            session.questions = cleaned[: session.question_count]
            session.status = InterviewStatus.IN_PROGRESS
            self._touch(session)
            self._persist()
            return session, session.questions[0]

    async def answer(
        self, interview_id: str, answer: str
    ) -> tuple[InterviewSession, AnswerEvaluation, str | None, bool]:
        async with self._lock:
            session = self._get(interview_id)
            self._require_status(session, InterviewStatus.IN_PROGRESS)
            is_follow_up = session.pending_follow_up is not None
            question = session.pending_follow_up or session.questions[
                session.current_question_index
            ]
            evaluation = await self._evaluate(session, question, answer, is_follow_up)
            answer_record = AnswerRecord(
                question=question,
                answer=answer.strip(),
                evaluation=evaluation,
                is_follow_up=is_follow_up,
            )

            follow_up_question = (
                evaluation.follow_up_question
                if not is_follow_up and evaluation.follow_up_needed
                else None
            )
            next_index = session.current_question_index + 1
            report: InterviewReport | None = None
            if not follow_up_question and next_index >= len(session.questions):
                report_session = replace(
                    session,
                    answers=[*session.answers, answer_record],
                )
                report = await self._build_report(report_session)

            session.answers.append(answer_record)

            next_question: str | None
            next_is_follow_up = False
            if follow_up_question:
                session.pending_follow_up = follow_up_question
                next_question = session.pending_follow_up
                next_is_follow_up = True
            else:
                session.pending_follow_up = None
                session.current_question_index = next_index
                if session.current_question_index < len(session.questions):
                    next_question = session.questions[session.current_question_index]
                else:
                    next_question = None
                    session.report = report
                    session.status = InterviewStatus.COMPLETED
            self._touch(session)
            self._persist()
            return session, evaluation, next_question, next_is_follow_up

    async def finish(self, interview_id: str) -> InterviewSession:
        async with self._lock:
            session = self._get(interview_id)
            if session.status == InterviewStatus.COMPLETED:
                return session
            if session.status != InterviewStatus.IN_PROGRESS:
                raise ServiceError(
                    status_code=409,
                    code="INTERVIEW_NOT_STARTED",
                    message="Interview must be started before it can be finished",
                )
            session.report = await self._build_report(session)
            session.status = InterviewStatus.COMPLETED
            session.pending_follow_up = None
            self._touch(session)
            self._persist()
            return session

    async def get(self, interview_id: str) -> InterviewSession:
        async with self._lock:
            return self._get(interview_id)

    async def list(self, limit: int = 50) -> list[InterviewSession]:
        async with self._lock:
            return sorted(
                self._sessions.values(),
                key=lambda session: session.updated_at,
                reverse=True,
            )[:limit]

    async def _evaluate(
        self,
        session: InterviewSession,
        question: str,
        answer: str,
        is_follow_up: bool,
    ) -> AnswerEvaluation:
        raw = await self._llm.generate(
            [
                ChatMessage(
                    role="system",
                    content=(
                        "你是一名严格但有帮助的面试官。只返回 JSON："
                        "{\"score\":0到100,\"feedback\":\"简洁反馈\","
                        "\"follow_up_needed\":false,\"follow_up_question\":null}。"
                        "只有回答明显缺少关键细节时才追问。"
                    ),
                ),
                ChatMessage(
                    role="user",
                    content=(
                        f"岗位：{session.position}\n问题：{question}\n回答：{answer}\n"
                        f"这是追问回答：{'是' if is_follow_up else '否'}。"
                        "如果这是追问回答，follow_up_needed 必须为 false。"
                    ),
                ),
            ]
        )
        data = self._parse_json(raw, "评估回答")
        try:
            score = max(0.0, min(100.0, float(data["score"])))
            feedback = str(data["feedback"]).strip()
        except (KeyError, TypeError, ValueError) as exc:
            raise ServiceError(
                status_code=502,
                code="INVALID_LLM_RESPONSE",
                message="回答评估缺少有效的 score 或 feedback",
            ) from exc
        follow_up_needed = bool(data.get("follow_up_needed", False)) and not is_follow_up
        follow_up = data.get("follow_up_question")
        follow_up_question = str(follow_up).strip() if follow_up else None
        if not feedback:
            self._invalid_llm("回答评估的 feedback 为空")
        return AnswerEvaluation(
            score=score,
            feedback=feedback,
            follow_up_needed=follow_up_needed and bool(follow_up_question),
            follow_up_question=follow_up_question if follow_up_needed else None,
        )

    async def _build_report(self, session: InterviewSession) -> InterviewReport:
        transcript = "\n\n".join(
            f"问题：{item.question}\n回答：{item.answer}\n"
            f"评分：{item.evaluation.score}\n反馈：{item.evaluation.feedback}"
            for item in session.answers
        ) or "候选人尚未回答问题。"
        raw = await self._llm.generate(
            [
                ChatMessage(
                    role="system",
                    content=(
                        "你负责生成客观的面试总结。只返回 JSON："
                        "{\"overall_score\":0到100,\"summary\":\"总结\","
                        "\"strengths\":[\"优势\"],\"improvements\":[\"改进项\"],"
                        "\"recommendation\":\"建议\"}。不要根据姓名等敏感属性判断。"
                    ),
                ),
                ChatMessage(
                    role="user",
                    content=f"岗位：{session.position}\n面试记录：\n{transcript}",
                ),
            ]
        )
        data = self._parse_json(raw, "生成面试报告")
        try:
            score = max(0.0, min(100.0, float(data["overall_score"])))
            summary = str(data["summary"]).strip()
        except (KeyError, TypeError, ValueError) as exc:
            raise ServiceError(
                status_code=502,
                code="INVALID_LLM_RESPONSE",
                message="面试报告缺少有效的 overall_score 或 summary",
            ) from exc
        if not summary:
            self._invalid_llm("面试报告的 summary 为空")
        return InterviewReport(
            overall_score=score,
            summary=summary,
            strengths=self._string_list(data.get("strengths")),
            improvements=self._string_list(data.get("improvements")),
            recommendation=str(data.get("recommendation", "")).strip(),
        )

    def _get(self, interview_id: str) -> InterviewSession:
        session = self._sessions.get(interview_id)
        if session is None:
            raise ServiceError(
                status_code=404,
                code="INTERVIEW_NOT_FOUND",
                message="Interview session was not found",
            )
        return session

    @staticmethod
    def _require_status(session: InterviewSession, expected: InterviewStatus) -> None:
        if session.status != expected:
            raise ServiceError(
                status_code=409,
                code="INVALID_INTERVIEW_STATE",
                message=f"Interview is '{session.status}', expected '{expected}'",
            )

    @staticmethod
    def _touch(session: InterviewSession) -> None:
        session.updated_at = datetime.now(UTC)

    @staticmethod
    def _parse_json(raw: str, action: str) -> dict[str, Any]:
        candidate = raw.strip()
        match = _JSON_FENCE.search(candidate)
        if match:
            candidate = match.group(1).strip()
        try:
            value = json.loads(candidate)
        except json.JSONDecodeError as exc:
            raise ServiceError(
                status_code=502,
                code="INVALID_LLM_RESPONSE",
                message=f"大模型在{action}时没有返回有效 JSON",
            ) from exc
        if not isinstance(value, dict):
            InterviewService._invalid_llm(f"大模型在{action}时返回的不是 JSON 对象")
        return value

    @staticmethod
    def _invalid_llm(message: str) -> None:
        raise ServiceError(
            status_code=502,
            code="INVALID_LLM_RESPONSE",
            message=message,
        )

    @staticmethod
    def _string_list(value: Any) -> list[str]:
        if not isinstance(value, list):
            return []
        return [str(item).strip() for item in value if str(item).strip()]

    def _persist(self) -> None:
        self._store_path.parent.mkdir(parents=True, exist_ok=True)
        payload = [self._session_to_dict(item) for item in self._sessions.values()]
        temporary = self._store_path.with_suffix(self._store_path.suffix + ".tmp")
        temporary.write_text(
            json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        temporary.replace(self._store_path)

    def _load(self) -> dict[str, InterviewSession]:
        if not self._store_path.exists():
            return {}
        try:
            values = json.loads(self._store_path.read_text(encoding="utf-8"))
            return {item["id"]: self._session_from_dict(item) for item in values}
        except (OSError, ValueError, KeyError, TypeError) as exc:
            raise RuntimeError(f"Failed to load interview store: {exc}") from exc

    @staticmethod
    def _session_to_dict(session: InterviewSession) -> dict[str, Any]:
        value = asdict(session)
        value["status"] = session.status.value
        value["created_at"] = session.created_at.isoformat()
        value["updated_at"] = session.updated_at.isoformat()
        return value

    @staticmethod
    def _session_from_dict(value: dict[str, Any]) -> InterviewSession:
        answers = [
            AnswerRecord(
                question=item["question"],
                answer=item["answer"],
                evaluation=AnswerEvaluation(**item["evaluation"]),
                is_follow_up=item.get("is_follow_up", False),
            )
            for item in value.get("answers", [])
        ]
        report_value = value.get("report")
        return InterviewSession(
            id=value["id"],
            candidate_name=value["candidate_name"],
            position=value["position"],
            question_count=value["question_count"],
            status=InterviewStatus(value["status"]),
            resume_text=value.get("resume_text", ""),
            resume_file_name=value.get("resume_file_name"),
            questions=value.get("questions", []),
            current_question_index=value.get("current_question_index", 0),
            pending_follow_up=value.get("pending_follow_up"),
            answers=answers,
            report=InterviewReport(**report_value) if report_value else None,
            created_at=datetime.fromisoformat(value["created_at"]),
            updated_at=datetime.fromisoformat(value["updated_at"]),
        )
