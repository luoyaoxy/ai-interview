FROM python:3.13-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

WORKDIR /service

COPY pyproject.toml README.md ./
COPY app ./app

RUN pip install --no-cache-dir .

RUN addgroup --system ragservice \
    && adduser --system --ingroup ragservice ragservice \
    && mkdir -p /service/data/uploads \
    && chown -R ragservice:ragservice /service/data

USER ragservice

EXPOSE 8000

HEALTHCHECK --interval=30s --timeout=5s --start-period=15s --retries=3 \
    CMD ["python", "-c", "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8000/health', timeout=3)"]

CMD ["python", "-m", "app.cli"]
