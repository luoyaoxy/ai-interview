import tomllib
from importlib.metadata import version as package_version
from pathlib import Path

from fastapi.testclient import TestClient

from app import __version__

SERVICE_ROOT = Path(__file__).resolve().parents[1]


def test_health_exposes_the_service_version(client: TestClient) -> None:
    response = client.get("/health")

    assert response.status_code == 200
    assert response.json()["version"] == __version__
    assert package_version("ai-interview-rag-service") == __version__


def test_release_files_use_the_same_service_version() -> None:
    with (SERVICE_ROOT / "pyproject.toml").open("rb") as pyproject_file:
        pyproject = tomllib.load(pyproject_file)
    compose = (SERVICE_ROOT / "compose.yaml").read_text(encoding="utf-8")
    openapi = (SERVICE_ROOT.parent / "docs/api/rag-service.openapi.yaml").read_text(
        encoding="utf-8"
    )

    assert pyproject["project"]["version"] == __version__
    assert f"ai-interview-rag-service:{__version__}" in compose
    assert f"version: {__version__}" in openapi


def test_unsafe_request_id_is_replaced(client: TestClient) -> None:
    response = client.get("/health", headers={"X-Request-ID": "unsafe request id"})

    request_id = response.headers["X-Request-ID"]
    assert request_id.startswith("req-")
    assert request_id != "unsafe request id"
