# Vercel Deployment Guide

Deploy the **Physics Synthesis Compiler** web chat interface to Vercel with serverless C compilation, Edge CDN frontend delivery, and client-side artifact downloads.

---

## Architecture Overview

```
                                      ┌──────────────────────────────────────────────┐
                                      │              Vercel Edge CDN                 │
User Browser ────────────────────────►│  Serves /index.html, /styles.css, /app.js    │
     │                                └──────────────────────────────────────────────┘
     │
     │ POST /api/chat
     ▼
┌────────────────────────────────────────────────────────────────────────────────────┐
│ Vercel Serverless Function (Python 3.12 / api/index.py)                            │
│                                                                                    │
│ 1. Invokes ./synth (pre-compiled during build.sh via GCC 11 on Amazon Linux 2023)  │
│ 2. ./synth parses the prompt with the offline C NLP frontend (no network)          │
│ 3. Generates schematic-ir.v1 -> binds JLCPCB parts -> DC nodal solver -> KiCad emit│
│ 4. Writes artifacts to /tmp/out_web/<run_id>/                                      │
│ 5. Returns JSON response containing both download links & direct file contents     │
└────────────────────────────────────────────────────────────────────────────────────┘
     │
     ▼
User Browser creates instant client-side Blob URLs for download buttons
(Schematic, Netlist, BOM, Snapshot, Verify, IR JSON).
```

---

## Prerequisites

- A [Vercel account](https://vercel.com/)

---

## Method 1: Deploy via GitHub / Git (Recommended)

1. **Push your code to GitHub / GitLab / Bitbucket**:
   ```bash
   git add .
   git commit -m "Add Vercel hosting support"
   git push origin main
   ```

2. **Import into Vercel**:
   - Go to [vercel.com/new](https://vercel.com/new).
   - Select your repository and click **Import**.
   - Vercel automatically detects `vercel.json`:
     - **Framework Preset**: `Other`
     - **Build Command**: `bash build.sh`
     - **Output Directory**: `public`

3. **Click Deploy** (no environment variables required):
   Vercel compiles the C11 `synth` binary using `gcc`, optimizes static assets to the Edge CDN, and deploys the serverless API.

---

## Method 2: Deploy via Vercel CLI

1. **Install Vercel CLI**:
   ```bash
   npm install -g vercel
   ```

2. **Deploy**:
   ```bash
   cd d:\physics-synthesis-compiler
   vercel
   ```

3. **Deploy to Production**:
   ```bash
   vercel --prod
   ```

---

## Key Files Added for Vercel

| File | Purpose |
|---|---|
| `vercel.json` | Vercel configuration (`buildCommand`, `outputDirectory: public`, function memory & timeouts, rewrites) |
| `build.sh` | Build script executed in Vercel's Amazon Linux 2023 container to compile `synth` with GCC and sync `public/` assets |
| `api/index.py` | Serverless function entrypoint routing `/api/chat`, `/api/health`, and `/api/runs/*` |
| `public/` | Static web assets (`index.html`, `styles.css`, `app.js`) served directly from Vercel Edge CDN |
| `requirements.txt` | Python manifest indicating standard library runtime |
| `.vercelignore` | Excludes local test runs, temporary files, and sensitive keys from the deployed bundle |

---

## Local Development vs. Production

The project seamlessly supports both workflows:

- **Local Development**:
  ```bash
  python web/server.py
  # http://127.0.0.1:8765/
  ```
  Outputs to `out_web/`.

- **Vercel Production**:
  Uses `public/` CDN caching, serverless `api/index.py`, and writes to `/tmp/out_web/`.
