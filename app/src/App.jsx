import React, { useState } from "react";
import "./App.css";
import DeskModel from "./DeskModel";
import useDesk from "./useDesk";

const defaultHost = import.meta.env.VITE_PI_HOST || (window.location.port === "5000" ? window.location.hostname : "sdg-pi.local");

function App() {
  const [address, setAddress] = useState(defaultHost);
  const [connection, setConnection] = useState({ host: defaultHost, attempt: 0 });
  const [inputError, setInputError] = useState("");
  function connect(event) {
    event.preventDefault();
    const value = address.trim().replace(/^(?:https?|wss?):\/\//i, "").replace(/\/$/, "").replace(/:9001$/, "");
    if (!/^[a-zA-Z0-9][a-zA-Z0-9.-]*$/.test(value)) {
      setInputError("Enter the Pi hostname or IPv4 address, for example sdg-pi.local.");
      return;
    }
    setInputError("");
    setAddress(value);
    setConnection(previous => ({ host: value, attempt: previous.attempt + 1 }));
  }
  const controls = <div className="connection">
    <form onSubmit={connect}>
      <label>Pi hostname or IP <input required value={address} onChange={event => setAddress(event.target.value)} spellCheck={false}/></label>
      <button type="submit">Connect</button>
    </form>
    {inputError && <p role="alert">{inputError}</p>}
  </div>;
  return <Dashboard key={connection.attempt} host={connection.host} controls={controls}/>;
}

function Dashboard({host, controls}) {
  const { data, received, status, connectionError, history, normal, errors, alerts } = useDesk(host);
  const close = data?.distance > 0 && data.distance < 50;
  const overdue = data?.sitting && data.sit_minutes >= 45;
  const dim = data && data.light < 200;
  const warnings = [
    overdue && "Time to stand up and stretch.",
    close && "Move a little farther from your screen.",
    dim && "Turn on a light near your desk.",
    ...alerts
  ].filter(Boolean);
  return <div className="shell">
    <header><div className="brand"><span className="logo"><Icon type="pulse"/></span>Smart Desk <b>Guardian</b></div>
      <span className="source"><i/>{status}</span></header>
    <main><section className="hero"><div className="hero-copy"><div className="eyebrow"><span className="rule"/>WORKSTATION MONITOR</div>
      <h1>Desk overview<span>.</span></h1><p>{"Source: " + host + (received ? " · Last received " + received.toLocaleTimeString() : " · Awaiting sensor data")}</p>
      </div><div className="diagram"><DeskModel/></div></section>
      {controls}
      {connectionError && <p className="connection-error" role="status">{connectionError}</p>}
      <section aria-labelledby="readings"><div className="section-title"><h2 id="readings">Desk readings</h2></div><div className="cards">
      <Card type="screen" title="Screen distance" value={data?.distance || "—"} unit="cm" state={!data?.distance ? "No reading" : close ? "Too close" : "In range"} bad={close}/>
      <Card type="clock" title="Sitting time" value={data ? (data.sitting ? data.sit_minutes : 0) : "—"} unit="min" state={!data ? "No reading" : overdue ? "Break due" : data.sitting ? "Seated" : "Standing"} bad={overdue}/>
      <Card type="light" title="Room light" value={data?.light ?? "—"} unit="/ 1023" state={!data ? "No reading" : dim ? "Too dark" : "Light OK"} bad={dim}/>
      <Card type="pulse" title="Heart rate" value={data?.bpm || "—"} unit="bpm" state={data?.bpm ? "Latest check" : "No recent check"}/>
      </div></section>
      <div className="alerts" aria-live="polite">{warnings.map((w,i)=><div className="warning" key={i}><Icon type="alert"/><span>{w}</span></div>)}</div>
      <div className="lower"><section className="panel"><h2>Your normal</h2>
      {normal ? <><p className="sub">{normal.learning ? "Learning your sitting limit · "+normal.days+" of 3 days" : "Limits from the Pi · "+normal.days+" days of sitting data"}</p>
      <dl>{[["clock","Sitting time",normal.sit_minutes,"min"],["pulse","Heart rate",normal.bpm,"bpm"]].map(([id,title,value,unit])=><div key={id}><dt><Icon type={id}/>{title}</dt><dd>{value} <small>{unit}</small></dd></div>)}</dl></>:
      <p className="sub">{errors.normal ? errors.normal:"Waiting for personal limits…"}</p>}</section>
      <section className="panel"><h2>Session history</h2>
      {history?.length ? <div className="session-list">{history.map((session, index) =>
        <details key={session.id ?? (session.ended + "-" + index)}>
          <summary>{session.ended} · {session.minutes} min</summary>
          <dl><div><dt>Session ended</dt><dd>{session.ended}</dd></div>
            <div><dt>Time seated</dt><dd>{session.minutes} min</dd></div></dl>
          {["distance", "light", "bpm"].some(key => session[key] != null)
            ? <><p className="sub">Last available seated readings</p><dl>
              {[["Screen distance", "distance", "cm"], ["Room light", "light", "/ 1023"], ["Heart rate", "bpm", "bpm"]].map(([title, key, unit]) =>
                <div key={key}><dt>{title}</dt><dd>{session[key] == null ? "Not recorded" : session[key] + " " + unit}</dd></div>)}
              </dl></>
            : <p className="sub">No sensor readings were saved for this session.</p>}
        </details>)}</div>:
      <p className="sub">{errors.history ? errors.history:history ? "No completed sessions." : "Waiting for session history…"}</p>}</section></div>
    </main></div>;
}
const paths={
  screen:"M3 4h18v13H3z M9 21h6m-3-4v4",
  clock:"M12 4a8 8 0 1 0 .01 0 M12 7v5l3 2",
  light:"M12 8a4 4 0 1 0 .01 0 M12 2v2m0 16v2M2 12h2m16 0h2M5 5l1.5 1.5m11 11L19 19M5 19l1.5-1.5m11-11L19 5",
  pulse:"M2 12h5l3-8 4 16 3-8h5",
  alert:"M12 3 22 20H2z M12 9v5m0 3v.1"
};
function Icon({type}){return <svg aria-hidden="true" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.65" strokeLinecap="round" strokeLinejoin="round"><path d={paths[type]}/></svg>}
function Card({type,title,value,unit,state,bad}) {
  return <article className={"card "+(bad?"bad":"")}><div className="card-title"><span className="icon"><Icon type={type}/></span><h3>{title}</h3></div>
    <div className="reading">{value}<small>{unit}</small></div><div className="state"><i/>{state}</div></article>;
}
export default App;
