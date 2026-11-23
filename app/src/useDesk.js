import { useEffect, useState } from "react";
import mqtt from "mqtt";

const nonnegative = value => Number.isFinite(value) && value >= 0;
const validReading = value => value && typeof value.sitting === "boolean"
  && ["distance", "light", "sit_minutes", "bpm"].every(key => nonnegative(value[key]))
  && value.light <= 1023;
const validHistory = value => Array.isArray(value) && value.every(session =>
  session && typeof session.ended === "string" && nonnegative(session.minutes));
const validNormal = value => value && typeof value.learning === "boolean"
  && ["sit_minutes", "bpm", "days"].every(key => nonnegative(value[key]));

export default function useDesk(host) {
  const [data, setData] = useState(null);
  const [received, setReceived] = useState(null);
  const [status, setStatus] = useState("Connecting…");
  const [connectionError, setConnectionError] = useState("");
  const [history, setHistory] = useState(null);
  const [normal, setNormal] = useState(null);
  const [errors, setErrors] = useState({});
  const [alerts, setAlerts] = useState({});

  useEffect(() => {
    let active = true;
    let staleTimer;
    let refreshing = false;
    const alertTimers = new Map();
    const abort = new AbortController();
    const url = "ws://" + host + ":9001";

    if (window.location.protocol === "https:") {
      setStatus("Connection blocked");
      setConnectionError("Open the downloaded HTML file or the app’s local HTTP address to connect to this Pi.");
      return;
    }

    const client = mqtt.connect(url, { connectTimeout: 6000, reconnectPeriod: 3000 });
    client.on("connect", () => {
      if (!active) return;
      setConnectionError("");
      setStatus("Connected · waiting for sensor readings");
      client.subscribe(["sdg/readings", "sdg/alerts"], (error, grants) => {
        if (!active) return;
        if (error || grants?.some(grant => grant.qos === 128)) {
          setConnectionError("The MQTT broker refused the sensor subscription.");
        }
      });
    });
    client.on("error", error => {
      if (active) setConnectionError("MQTT connection failed: " + error.message);
    });
    client.on("close", () => {
      if (!active) return;
      clearTimeout(staleTimer);
      alertTimers.forEach(clearTimeout);
      alertTimers.clear();
      setAlerts({});
      setData(null);
      setStatus("MQTT disconnected · retrying");
      setConnectionError("Cannot connect to " + url + ". Check the Pi address, Wi-Fi and Mosquitto WebSocket listener on port 9001.");
    });
    client.on("message", (topic, bytes, packet) => {
      if (!active || packet.retain) return; // The team's messages have no timestamps.
      let message;
      try { message = JSON.parse(bytes.toString()); } catch { return; }
      if (topic === "sdg/readings") {
        if (!validReading(message)) {
          setConnectionError("The Pi sent an invalid sensor message.");
          return;
        }
        setConnectionError("");
        setData(message);
        setReceived(new Date());
        setStatus("Live sensor readings");
        clearTimeout(staleTimer);
        // The Arduino pauses readings during its 20-second ECG check and result display.
        staleTimer = setTimeout(() => {
          if (!active) return;
          setData(null);
          setStatus("Connected · sensor readings delayed");
        }, 30000);
      }
      if (topic === "sdg/alerts" && typeof message?.msg === "string") {
        const key = message.type || message.msg;
        clearTimeout(alertTimers.get(key));
        setAlerts(previous => ({ ...previous, [key]: message.msg }));
        alertTimers.set(key, setTimeout(() => {
          if (!active) return;
          setAlerts(previous => {
            const next = { ...previous };
            delete next[key];
            return next;
          });
          alertTimers.delete(key);
        }, 60000));
      }
    });

    async function load(path, save, valid) {
      try {
        const response = await fetch("http://" + host + ":5000/" + path, {
          cache: "no-store",
          signal: AbortSignal.any([abort.signal, AbortSignal.timeout(6000)])
        });
        if (!response.ok) throw new Error("HTTP " + response.status);
        const result = await response.json();
        if (!valid(result)) throw new Error("Invalid response");
        if (!active) return;
        save(result);
        setErrors(previous => ({ ...previous, [path]: "" }));
      } catch (error) {
        if (!active) return;
        save(null);
        setErrors(previous => ({ ...previous, [path]:
          "Could not load /" + path + " from port 5000. Check the Pi API and browser network permission. Retrying automatically." }));
      }
    }
    async function refresh() {
      if (refreshing) return;
      refreshing = true;
      await Promise.all([
        load("history", setHistory, validHistory),
        load("normal", setNormal, validNormal)
      ]);
      refreshing = false;
    }
    refresh();
    const refreshTimer = setInterval(refresh, 15000);
    return () => {
      active = false;
      clearInterval(refreshTimer);
      clearTimeout(staleTimer);
      alertTimers.forEach(clearTimeout);
      abort.abort();
      client.end(true);
    };
  }, [host]);

  return { data, received, status, connectionError, history, normal, errors, alerts: Object.values(alerts) };
}
