# Railway / container deploy — long-running web UI + synth (not Vercel serverless).
FROM python:3.12-slim-bookworm

RUN apt-get update \
  && apt-get install -y --no-install-recommends gcc curl \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN bash build.sh \
  && test -x ./synth \
  && ./synth help >/dev/null

# Do not set VERCEL — keeps generate timeout at 300s.
ENV PYTHONUNBUFFERED=1
ENV PORT=8080

EXPOSE 8080

CMD ["python", "web/server.py"]
