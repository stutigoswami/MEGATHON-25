(function () {
  const $ = (sel) => document.querySelector(sel);
  const log = $("#chat-log");
  const nameInput = $("#name");
  const msgInput = $("#message");
  const sendBtn = $("#send");
  const dot = $("#status-dot");
  const statusText = $("#status-text");

  // Generate a short human-ish ID if user doesn't set a name
  function shortId() {
    const words = ["nova","pixel","ember","luna","atlas","raven","ion","zen","echo","vibe","flux","sage"];
    const w = words[Math.floor(Math.random() * words.length)];
    const n = Math.floor(100 + Math.random() * 900);
    return `${w}-${n}`;
  }

  nameInput.value = nameInput.value || shortId();

  const wsUrl = (location.protocol === "https:" ? "wss://" : "ws://") + location.host + "/ws";
  let ws;

  function setStatus(connected) {
    statusText.textContent = connected ? "Connected" : "Disconnected";
    dot.classList.toggle("dot-green", connected);
    dot.classList.toggle("dot-red", !connected);
  }

  function appendSystem(text) {
    const el = document.createElement("div");
    el.className = "msg";
    el.innerHTML = `
      <div class="avatar">ℹ️</div>
      <div class="meta">System</div>
      <div class="text">${text}</div>
    `;
    log.appendChild(el);
    log.scrollTop = log.scrollHeight;
  }

  function appendMessage(msg, mine = false) {
    const time = new Date(msg.time || Date.now());
    const hh = String(time.getHours()).padStart(2, "0");
    const mm = String(time.getMinutes()).padStart(2, "0");
    const initials = (msg.user || "?").slice(0, 2).toUpperCase();

    const el = document.createElement("div");
    el.className = "msg" + (mine ? " mine" : "");
    el.innerHTML = `
      <div class="avatar">${initials}</div>
      <div class="meta">${msg.user || "Anon"} • ${hh}:${mm}</div>
      <div class="text"></div>
    `;
    el.querySelector(".text").textContent = msg.text || "";
    log.appendChild(el);
    log.scrollTop = log.scrollHeight;
  }

  function connect() {
    ws = new WebSocket(wsUrl);

    ws.addEventListener("open", () => {
      setStatus(true);
      // Send a presence ping so others see "joined" via server system message already
      // Optionally announce ourselves with a lightweight client message (not persisted)
      // Do nothing: server sends a system message on join
    });

    ws.addEventListener("message", (ev) => {
      try {
        const data = JSON.parse(ev.data);
        if (data.type === "system") {
          appendSystem(data.text || "");
        } else if (data.type === "chat") {
          appendMessage(data, false);
        }
      } catch {
        // Fallback if server sent plain text
        appendMessage({ user: "Unknown", text: String(ev.data), time: Date.now() }, false);
      }
    });

    ws.addEventListener("close", () => {
      setStatus(false);
      appendSystem("Connection lost. Reconnecting in 1.5s…");
      setTimeout(connect, 1500);
    });

    ws.addEventListener("error", () => {
      // Handled by close handler for retry
    });
  }

  function sendCurrent() {
    const user = nameInput.value.trim() || shortId();
    const text = msgInput.value.trim();
    if (!text) return;

    const payload = JSON.stringify({
      type: "chat",
      user,
      text,
      time: Date.now(),
    });

    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(payload);
      appendMessage({ user, text, time: Date.now() }, true);
      msgInput.value = "";
      msgInput.focus();
    } else {
      appendSystem("Not connected.");
    }
  }

  sendBtn.addEventListener("click", sendCurrent);
  msgInput.addEventListener("keydown", (e) => {
    if (e.key === "Enter") {
      e.preventDefault();
      sendCurrent();
    }
  });

  connect();
})();
