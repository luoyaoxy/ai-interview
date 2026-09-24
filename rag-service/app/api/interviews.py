"""AI interview session HTTP routes."""

from pathlib import Path
from typing import Annotated
from uuid import uuid4

from fastapi import APIRouter, Depends, File, Query, Request, UploadFile, status

from app.api.dependencies import Authorized
from app.api.schemas import (
    CreateInterviewRequest,
    Interview,
    InterviewAnswer,
    InterviewEvaluation,
    InterviewPageResponse,
    InterviewReport,
    InterviewReportResponse,
    InterviewResponse,
    InterviewTurnResponse,
    SubmitInterviewAnswerRequest,
)
from app.core.errors import ServiceError
from app.interview.models import InterviewSession
from app.services import ServiceContainer, get_services

router = APIRouter(prefix="/interviews", tags=["Interviews"])
Services = Annotated[ServiceContainer, Depends(get_services)]


def to_interview(session: InterviewSession) -> Interview:
    return Interview(
        id=session.id,
        candidate_name=session.candidate_name,
        position=session.position,
        question_count=session.question_count,
        status=session.status.value,
        resume_file_name=session.resume_file_name,
        questions=session.questions,
        current_question_index=session.current_question_index,
        current_question=(
            session.pending_follow_up
            or (
                session.questions[session.current_question_index]
                if session.status.value == "in_progress"
                and session.current_question_index < len(session.questions)
                else None
            )
        ),
        is_follow_up=session.pending_follow_up is not None,
        answers=[
            InterviewAnswer(
                question=item.question,
                answer=item.answer,
                evaluation=InterviewEvaluation(
                    score=item.evaluation.score,
                    feedback=item.evaluation.feedback,
                    follow_up_needed=item.evaluation.follow_up_needed,
                    follow_up_question=item.evaluation.follow_up_question,
                ),
                is_follow_up=item.is_follow_up,
            )
            for item in session.answers
        ],
        report=(
            InterviewReport(
                overall_score=session.report.overall_score,
                summary=session.report.summary,
                strengths=session.report.strengths,
                improvements=session.report.improvements,
                recommendation=session.report.recommendation,
            )
            if session.report
            else None
        ),
        created_at=session.created_at,
        updated_at=session.updated_at,
    )


@router.get("", operation_id="listInterviews", response_model=InterviewPageResponse)
async def list_interviews(
    _authorization: Authorized,
    services: Services,
    request: Request,
    limit: Annotated[int, Query(ge=1, le=100)] = 50,
) -> InterviewPageResponse:
    sessions = await services.interviews.list(limit)
    return InterviewPageResponse(
        request_id=request.state.request_id,
        items=[to_interview(session) for session in sessions],
        total=len(sessions),
    )


@router.post(
    "",
    operation_id="createInterview",
    response_model=InterviewResponse,
    status_code=status.HTTP_201_CREATED,
)
async def create_interview(
    body: CreateInterviewRequest,
    _authorization: Authorized,
    services: Services,
    request: Request,
) -> InterviewResponse:
    session = await services.interviews.create(
        body.candidate_name, body.position, body.question_count
    )
    return InterviewResponse(
        request_id=request.state.request_id, interview=to_interview(session)
    )


@router.post(
    "/{interview_id}/resume",
    operation_id="uploadInterviewResume",
    response_model=InterviewResponse,
)
async def upload_resume(
    interview_id: str,
    _authorization: Authorized,
    services: Services,
    request: Request,
    file: Annotated[UploadFile, File()],
) -> InterviewResponse:
    original_name = Path(file.filename or "").name
    if not original_name:
        raise ServiceError(
            status_code=400, code="MISSING_FILE_NAME", message="Resume must have a file name"
        )
    extension = Path(original_name).suffix.lower()
    if extension not in services.settings.supported_extensions:
        raise ServiceError(
            status_code=415,
            code="UNSUPPORTED_DOCUMENT_TYPE",
            message=f"Unsupported resume extension '{extension}'",
        )
    destination_dir = services.settings.upload_dir / "interviews" / interview_id
    destination_dir.mkdir(parents=True, exist_ok=True)
    destination = destination_dir / f"{uuid4()}-{original_name}"
    total_bytes = 0
    try:
        with destination.open("wb") as output:
            while content := await file.read(1024 * 1024):
                total_bytes += len(content)
                if total_bytes > services.settings.max_upload_bytes:
                    raise ServiceError(
                        status_code=413,
                        code="DOCUMENT_TOO_LARGE",
                        message="Uploaded resume exceeds the configured size limit",
                    )
                output.write(content)
        session = await services.interviews.attach_resume(
            interview_id, destination, original_name
        )
    except Exception:
        destination.unlink(missing_ok=True)
        raise
    finally:
        await file.close()
    return InterviewResponse(
        request_id=request.state.request_id, interview=to_interview(session)
    )


@router.post(
    "/{interview_id}/start",
    operation_id="startInterview",
    response_model=InterviewTurnResponse,
)
async def start_interview(
    interview_id: str,
    _authorization: Authorized,
    services: Services,
    request: Request,
) -> InterviewTurnResponse:
    session, question = await services.interviews.start(interview_id)
    return InterviewTurnResponse(
        request_id=request.state.request_id,
        interview=to_interview(session),
        question=question,
        question_number=1,
        total_questions=session.question_count,
    )


@router.post(
    "/{interview_id}/answers",
    operation_id="submitInterviewAnswer",
    response_model=InterviewTurnResponse,
)
async def submit_answer(
    interview_id: str,
    body: SubmitInterviewAnswerRequest,
    _authorization: Authorized,
    services: Services,
    request: Request,
) -> InterviewTurnResponse:
    session, evaluation, question, is_follow_up = await services.interviews.answer(
        interview_id, body.answer
    )
    question_number = min(session.current_question_index + 1, session.question_count)
    return InterviewTurnResponse(
        request_id=request.state.request_id,
        interview=to_interview(session),
        question=question,
        question_number=question_number,
        total_questions=session.question_count,
        is_follow_up=is_follow_up,
        evaluation=InterviewEvaluation(
            score=evaluation.score,
            feedback=evaluation.feedback,
            follow_up_needed=evaluation.follow_up_needed,
            follow_up_question=evaluation.follow_up_question,
        ),
    )


@router.get(
    "/{interview_id}", operation_id="getInterview", response_model=InterviewResponse
)
async def get_interview(
    interview_id: str,
    _authorization: Authorized,
    services: Services,
    request: Request,
) -> InterviewResponse:
    session = await services.interviews.get(interview_id)
    return InterviewResponse(
        request_id=request.state.request_id, interview=to_interview(session)
    )


@router.post(
    "/{interview_id}/finish",
    operation_id="finishInterview",
    response_model=InterviewResponse,
)
async def finish_interview(
    interview_id: str,
    _authorization: Authorized,
    services: Services,
    request: Request,
) -> InterviewResponse:
    session = await services.interviews.finish(interview_id)
    return InterviewResponse(
        request_id=request.state.request_id, interview=to_interview(session)
    )


@router.get(
    "/{interview_id}/report",
    operation_id="getInterviewReport",
    response_model=InterviewReportResponse,
)
async def get_interview_report(
    interview_id: str,
    _authorization: Authorized,
    services: Services,
    request: Request,
) -> InterviewReportResponse:
    session = await services.interviews.get(interview_id)
    if session.report is None:
        raise ServiceError(
            status_code=409,
            code="REPORT_NOT_READY",
            message="Interview report is not ready",
        )
    return InterviewReportResponse(
        request_id=request.state.request_id,
        interview_id=session.id,
        report=InterviewReport(
            overall_score=session.report.overall_score,
            summary=session.report.summary,
            strengths=session.report.strengths,
            improvements=session.report.improvements,
            recommendation=session.report.recommendation,
        ),
    )
