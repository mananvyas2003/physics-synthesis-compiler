# Deploy on Railway

Long-running service (not serverless). Generate timeout stays **300s** (do not set `VERCEL=1`).

## 1. Push this repo to GitHub

## 2. New project on [railway.app](https://railway.app)

- **New Project** → **Deploy from GitHub repo**
- Railway uses `Dockerfile` + `railway.toml`

## 3. Variables

| Variable | Value |
|----------|--------|
| `GEMINI_API_KEY` | Google AI Studio key (`AIza…`) |

Do **not** set `VERCEL`. Railway injects `PORT` automatically.

## 4. Generate domain

Settings → Networking → **Generate domain**. Open the URL (chat UI).

## 5. Local Docker smoke (optional)

```bash
docker build -t synth-web .
docker run --rm -p 8080:8080 -e GEMINI_API_KEY=AIza... -e PORT=8080 synth-web
```

Open http://127.0.0.1:8080/

## Notes

- Build compiles `synth` with gcc inside the image (`build.sh`).
- `curl` is installed for live Gemini.
- Artifacts land under `out_web/` in the container (ephemeral unless you add a volume).
- Railway is usage/credits-based after trial — not forever-free.
