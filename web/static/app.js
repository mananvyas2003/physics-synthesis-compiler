const chat = document.getElementById("chat");
const form = document.getElementById("form");
const promptEl = document.getElementById("prompt");
const sendBtn = document.getElementById("send");
const statusEl = document.getElementById("status");

function setStatus(text) {
  statusEl.textContent = text;
}

function ensureEmpty() {
  if (chat.children.length === 0) {
    const empty = document.createElement("div");
    empty.className = "empty";
    empty.id = "empty";
    empty.innerHTML =
      "<h1>Describe a circuit.</h1><p>The offline NLP frontend drafts schematic IR; the compiler binds, verifies, and emits KiCad.</p>";
    chat.appendChild(empty);
  }
}

function clearEmpty() {
  const empty = document.getElementById("empty");
  if (empty) empty.remove();
}

function addBubble(role, text, opts = {}) {
  clearEmpty();
  const el = document.createElement("article");
  el.className = `bubble ${role}${opts.error ? " error" : ""}`;
  const who = document.createElement("p");
  who.className = "who";
  who.textContent = role === "user" ? "You" : "Synthesis";
  const body = document.createElement("p");
  body.className = "text";
  body.textContent = text;
  el.appendChild(who);
  el.appendChild(body);

  if (opts.artifacts) {
    const row = document.createElement("div");
    row.className = "artifacts";
    const labels = {
      "design.kicad_sch": "Schematic",
      "design.net": "Netlist",
      "bom.csv": "BOM",
      "design-snapshot.v1.json": "Snapshot",
      "verification.v1.json": "Verify",
      "prompt_schematic.json": "IR JSON",
      "composed_schematic.json": "Composed JSON",
      "mfg-dfm.v1.json": "DFM Report",
    };
    Object.entries(opts.artifacts).forEach(([name, href]) => {
      const a = document.createElement("a");
      if (opts.artifact_contents && opts.artifact_contents[name]) {
        const mime = name.endsWith(".json")
          ? "application/json;charset=utf-8"
          : name.endsWith(".csv")
          ? "text/csv;charset=utf-8"
          : "text/plain;charset=utf-8";
        const blob = new Blob([opts.artifact_contents[name]], { type: mime });
        a.href = URL.createObjectURL(blob);
      } else {
        a.href = href;
      }
      a.target = "_blank";
      a.rel = "noopener";
      a.textContent = labels[name] || name;
      a.download = name;
      row.appendChild(a);
    });
    el.appendChild(row);
  }

  if (opts.meta) {
    const meta = document.createElement("p");
    meta.className = "meta";
    meta.textContent = opts.meta;
    el.appendChild(meta);
  }

  chat.appendChild(el);
  el.scrollIntoView({ behavior: "smooth", block: "end" });
  return el;
}

ensureEmpty();

const catStatus = document.getElementById("cat-status");
const partsFile = document.getElementById("parts-file");
const dfmFile = document.getElementById("dfm-file");

async function refreshCatalogue() {
  try {
    const res = await fetch("/api/catalogue");
    const data = await res.json();
    catStatus.textContent = `${data.parts ?? "?"} DEMO/user parts · DFM “${
      data.dfm_name || "standard"
    }”`;
  } catch (err) {
    catStatus.textContent = "Catalogue unavailable";
  }
}

partsFile?.addEventListener("change", async () => {
  const file = partsFile.files?.[0];
  if (!file) return;
  setStatus("Importing parts…");
  try {
    const buf = await file.arrayBuffer();
    const res = await fetch("/api/upload/parts", {
      method: "POST",
      headers: { "Content-Type": "text/csv" },
      body: buf,
    });
    const data = await res.json();
    if (!data.ok) throw new Error(data.error || "import failed");
    setStatus("Parts imported");
    await refreshCatalogue();
  } catch (err) {
    setStatus("Parts import failed");
    addBubble("bot", String(err), { error: true });
  } finally {
    partsFile.value = "";
  }
});

dfmFile?.addEventListener("change", async () => {
  const file = dfmFile.files?.[0];
  if (!file) return;
  setStatus("Updating DFM…");
  try {
    const text = await file.text();
    const res = await fetch("/api/upload/dfm", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: text,
    });
    const data = await res.json();
    if (!data.ok) throw new Error(data.error || "DFM upload failed");
    setStatus("DFM profile saved");
    await refreshCatalogue();
  } catch (err) {
    setStatus("DFM upload failed");
    addBubble("bot", String(err), { error: true });
  } finally {
    dfmFile.value = "";
  }
});

refreshCatalogue();

async function pollGenerateJob(jobId, onTick) {
  const deadline = Date.now() + 180000;
  while (Date.now() < deadline) {
    const res = await fetch(`/api/jobs/${jobId}`);
    const job = await res.json();
    if (!res.ok || !job.ok) {
      throw new Error((job && job.error) || `job poll failed (${res.status})`);
    }
    if (typeof onTick === "function") onTick(job.status || "…");
    if (job.status === "done" || job.status === "error") {
      return job.result || { ok: false, error: "missing job result" };
    }
    await new Promise((r) => setTimeout(r, 500));
  }
  throw new Error("generate timed out waiting for job");
}

form.addEventListener("submit", async (e) => {
  e.preventDefault();
  const prompt = promptEl.value.trim();
  if (!prompt) return;

  addBubble("user", prompt);
  promptEl.value = "";
  sendBtn.disabled = true;
  setStatus("Queued…");
  const thinking = addBubble("bot", "Queued generate job…");

  try {
    const res = await fetch("/api/generate", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ prompt }),
    });
    const enq = await res.json();
    if (!res.ok || !enq.ok || !enq.job_id) {
      thinking.remove();
      addBubble("bot", (enq && enq.error) || "Failed to enqueue generate.", {
        error: true,
      });
      setStatus("Failed");
      return;
    }

    setStatus(`Job ${enq.job_id.slice(0, 8)}…`);
    thinking.querySelector(".text").textContent =
      "NLP → bind → verify → emit…";
    const data = await pollGenerateJob(enq.job_id, (st) => {
      setStatus(`Job ${st}…`);
      thinking.querySelector(".text").textContent = `Status: ${st}`;
    });
    thinking.remove();

    if (!data.ok) {
      addBubble("bot", data.error || "Generation failed.", { error: true });
      setStatus("Failed");
      return;
    }

    const lines = [
      "Schematic generated.",
      data.verification_summary
        ? `Verification: ${data.verification_summary}`
        : null,
      data.run_id ? `Run: ${data.run_id}` : null,
    ].filter(Boolean);

    addBubble("bot", lines.join("\n"), {
      artifacts: data.artifacts,
      artifact_contents: data.artifact_contents,
      meta: "Open Schematic in KiCad, or download the other artifacts.",
    });
    setStatus("Ready");
  } catch (err) {
    thinking.remove();
    addBubble("bot", String(err), { error: true });
    setStatus("Error");
  } finally {
    sendBtn.disabled = false;
    promptEl.focus();
  }
});

promptEl.addEventListener("keydown", (e) => {
  if (e.key === "Enter" && !e.shiftKey) {
    e.preventDefault();
    form.requestSubmit();
  }
});
