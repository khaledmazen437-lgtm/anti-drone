import React, { useEffect, useRef, useState } from "react";
import { io } from "socket.io-client";

const BACKEND_URL = "http://localhost:4000";

const STATE_META = {
  SCANNING: { label: "SCANNING", color: "var(--amber)" },
  ALERT: { label: "ALERT", color: "var(--red)" },
  JAMMING: { label: "JAMMING", color: "var(--cyan)" },
  DISCONNECTED: { label: "NO LINK", color: "var(--muted)" },
};

export default function App() {
  const [telemetry, setTelemetry] = useState({
    status: "DISCONNECTED",
    energy_level: 0,
    threat_detected: false,
  });
  const [connected, setConnected] = useState(false);
  const [sending, setSending] = useState(false);
  const [log, setLog] = useState([]);
  const socketRef = useRef(null);

  useEffect(() => {
    const socket = io(BACKEND_URL);
    socketRef.current = socket;

    socket.on("connect", () => setConnected(true));
    socket.on("disconnect", () => setConnected(false));
    socket.on("telemetry", (data) => {
      setTelemetry(data);
    });

    return () => socket.disconnect();
  }, []);

  const sendCommand = async (command) => {
    setSending(true);
    try {
      const res = await fetch(`${BACKEND_URL}/api/command`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ command }),
      });
      const data = await res.json();
      const timestamp = new Date().toLocaleTimeString();
      setLog((prev) => [
        { timestamp, command, ok: res.ok, detail: data.error || "sent" },
        ...prev.slice(0, 7),
      ]);
    } catch (e) {
      const timestamp = new Date().toLocaleTimeString();
      setLog((prev) => [
        { timestamp, command, ok: false, detail: "backend unreachable" },
        ...prev.slice(0, 7),
      ]);
    } finally {
      setSending(false);
    }
  };

  const meta = STATE_META[telemetry.status] || STATE_META.DISCONNECTED;
  const energy = Math.max(0, Math.min(100, telemetry.energy_level || 0));
  const segments = 20;
  const litSegments = Math.round((energy / 100) * segments);

  return (
    <div className="console">
      <header className="console__header">
        <div className="console__title">
          <span className="eyebrow">ITC-EGYPT // SOLID-STATE COUNTER-UAS</span>
          <h1>SENSOR NODE-01</h1>
        </div>
        <div className={`link-indicator ${connected ? "is-live" : "is-down"}`}>
          <span className="link-dot" />
          {connected ? "BACKEND LINK" : "NO BACKEND"}
        </div>
      </header>

      <main className="console__grid">
        {/* Radar / status panel */}
        <section className="panel panel--radar">
          <div className="radar" style={{ "--sweep-color": meta.color }}>
            <div className="radar__rings" />
            {telemetry.status === "SCANNING" && <div className="radar__sweep" />}
            {telemetry.status === "JAMMING" && <div className="radar__noise" />}
            <div className="radar__core">
              <span className="radar__status" style={{ color: meta.color }}>
                {meta.label}
              </span>
              {telemetry.threat_detected && (
                <span className="radar__threat-tag">THREAT FLAGGED</span>
              )}
            </div>
          </div>
        </section>

        {/* Energy level panel */}
        <section className="panel panel--energy">
          <h2 className="panel__label">RF ENERGY LEVEL</h2>
          <div className="energy-bar">
            {Array.from({ length: segments }).map((_, i) => {
              const lit = i < litSegments;
              const zone = i >= 16 ? "hi" : i >= 10 ? "mid" : "lo";
              return (
                <div
                  key={i}
                  className={`energy-bar__seg ${lit ? `is-lit zone-${zone}` : ""}`}
                />
              );
            })}
          </div>
          <div className="energy-readout">{energy}<span>%</span></div>
        </section>

        {/* Command panel */}
        <section className="panel panel--commands">
          <h2 className="panel__label">ENGAGEMENT CONTROL</h2>
          <div className="commands">
            <button
              className="btn btn--jam"
              disabled={sending}
              onClick={() => sendCommand("ENGAGE_JAMMER")}
            >
              ENGAGE JAMMER
            </button>
            <button
              className="btn btn--standby"
              disabled={sending}
              onClick={() => sendCommand("STANDBY")}
            >
              STANDBY
            </button>
          </div>

          <h2 className="panel__label panel__label--log">COMMAND LOG</h2>
          <ul className="log">
            {log.length === 0 && <li className="log__empty">No commands sent yet.</li>}
            {log.map((entry, idx) => (
              <li key={idx} className={entry.ok ? "log__ok" : "log__fail"}>
                <span className="log__time">{entry.timestamp}</span>
                <span className="log__cmd">{entry.command}</span>
                <span className="log__detail">{entry.detail}</span>
              </li>
            ))}
          </ul>
        </section>
      </main>
    </div>
  );
}
