// Kết nối tới board: qua broker MQTT trên cloud (EMQX Cloud, HiveMQ..., MQTT over WebSocket/TLS) hoặc bộ giả lập trong trình duyệt.
import { useCallback, useEffect, useRef, useState } from "react"
import type { MqttClient } from "mqtt"
import { LAMP_NAMES } from "@/config"
import {
  ALERT_TEXT, CLASS_TEXT, createLampModel, FAULT_TEXT, MODE_TEXT, T, toMs,
  type AlertPayload, type Config, type HistoryPayload, type Lamp, type StatePayload,
} from "@/lib/protocol"
import { createSimBoard, type SimBoard } from "@/lib/sim"

export type Login = { host: string; port: number; username: string; password: string }
export type ConnStatus = "idle" | "connecting" | "connected" | "error"
export type LogEntry = { id: number; at: number; text: string; kind: "bad" | "ok" | "info" }
export type SeriesPoint = { t: number; v: number[] }

const OFFLINE_MS = 15_000 // board gửi mỗi 5 s; 15 s không có gì thì coi như mất kết nối
const SERIES_MAX = 180
const LOG_KEY = "dencongcong-lich-su"
const LOG_MAX = 300

function loadLog(): LogEntry[] {
  try {
    const v = JSON.parse(localStorage.getItem(LOG_KEY) ?? "[]")
    return Array.isArray(v) ? v.slice(0, LOG_MAX) : []
  } catch {
    return []
  }
}
function saveLog(l: LogEntry[]) {
  try {
    localStorage.setItem(LOG_KEY, JSON.stringify(l))
  } catch {
    /* trình duyệt chặn lưu trữ */
  }
}

function mqttError(e: Error & { code?: number }) {
  const m = e.message || ""
  if (e.code === 4 || e.code === 5 || /not authorized|bad user/i.test(m)) return "Sai tên đăng nhập hoặc mật khẩu MQTT"
  if (/ENOTFOUND|getaddrinfo|websocket error|connack timeout|timeout/i.test(m)) return "Không kết nối được tới máy chủ MQTT. Kiểm tra địa chỉ broker và cổng (EMQX: 8084)"
  return m || "Lỗi kết nối MQTT"
}

export function useStation() {
  const [status, setStatus] = useState<ConnStatus>("idle")
  const [error, setError] = useState<string | null>(null)
  const [source, setSource] = useState<"sim" | "mqtt" | null>(null)
  const [boardOnline, setBoardOnline] = useState<boolean | null>(null)
  const [lastAt, setLastAt] = useState<number | null>(null)
  const [state, setState] = useState<StatePayload | null>(null)
  const [config, setConfig] = useState<Config | null>(null)
  const [lamps, setLamps] = useState<Lamp[] | null>(null)
  const [history24, setHistory24] = useState<HistoryPayload | null>(null)
  const [alert, setAlert] = useState<AlertPayload | null>(null)
  const [image, setImage] = useState<string | null>(null)
  const [current, setCurrent] = useState<SeriesPoint[]>([])
  const [lux, setLux] = useState<SeriesPoint[]>([])
  const [log, setLog] = useState<LogEntry[]>(loadLog)
  const [notice, setNotice] = useState<string | null>(null)

  const client = useRef<MqttClient | null>(null)
  const sim = useRef<SimBoard | null>(null)
  const model = useRef(createLampModel())
  const prev = useRef({ fault: [0, 0, 0, 0], night: null as boolean | null, mode: null as string | null, alertId: null as string | null, online: null as boolean | null })
  const pendingCfg = useRef<Partial<Config> | null>(null)
  const cfgRef = useRef<Config | null>(null)
  const lastSeen = useRef(0)

  const addLog = useCallback((text: string, kind: LogEntry["kind"] = "info") => {
    setLog((l) => {
      const id = (l[0]?.id ?? 0) + 1
      const out = [{ id, at: Date.now(), text, kind }, ...l].slice(0, LOG_MAX)
      saveLog(out)
      return out
    })
  }, [])

  const markOnline = useCallback(
    (v: boolean) => {
      if (prev.current.online === v) return
      prev.current.online = v
      setBoardOnline(v)
      addLog(v ? "Board ESP32 đang gửi dữ liệu" : "Không còn nhận được dữ liệu từ board ESP32", v ? "ok" : "bad")
    },
    [addLog],
  )

  /** Xử lý 1 tin MQTT (thật hoặc giả lập) */
  const onMessage = useCallback(
    (topic: string, text: string) => {
      const json = () => {
        try {
          return JSON.parse(text)
        } catch {
          return null
        }
      }
      const now = Date.now()
      if (topic === T.online) {
        if (text === "online" || text === "offline") markOnline(text === "online")
      } else if (topic === T.state) {
        const s = json() as StatePayload | null
        if (!s) return
        lastSeen.current = now
        if (s.duty) model.current.setDuty(s.duty)
        setState(s)
        if (typeof s.lux === "number") setLux((a) => [...a, { t: now, v: [s.lux!] }].slice(-SERIES_MAX))
        const p = prev.current
        if (typeof s.night === "boolean" && p.night !== null && s.night !== p.night) addLog(s.night ? "Trời tối: chuyển sang chế độ đêm, đèn nền 20%" : "Trời sáng: tắt đèn", "info")
        if (typeof s.night === "boolean") p.night = s.night
        if (s.mode && p.mode && s.mode !== p.mode) addLog(`Chế độ: ${MODE_TEXT[s.mode] ?? s.mode}`, s.mode === "schedule" ? "bad" : "info")
        if (s.mode) p.mode = s.mode
      } else if (topic === T.energy) {
        const e = json()
        if (!e || !Array.isArray(e.lamps)) return
        lastSeen.current = now
        markOnline(true)
        const ls = model.current.ingest(e)
        setLamps(ls)
        setLastAt(now)
        setCurrent((a) => [...a, { t: now, v: ls.map((l) => l.i ?? 0) }].slice(-SERIES_MAX))
        ls.forEach((l, k) => {
          const was = prev.current.fault[k]
          if (l.fault && !was) addLog(`Đèn ${k + 1} (${LAMP_NAMES[k]}): ${l.msg || FAULT_TEXT[l.fault]}`, "bad")
          if (!l.fault && was) addLog(`Đèn ${k + 1} (${LAMP_NAMES[k]}) đã hoạt động lại bình thường`, "ok")
          prev.current.fault[k] = l.fault
        })
      } else if (topic === T.config) {
        const c = json() as Config | null
        if (!c) return
        const before = cfgRef.current
        cfgRef.current = c
        setConfig(c)
        const want = pendingCfg.current
        if (want && before) {
          pendingCfg.current = null
          if (want.lux_night !== undefined && want.lux_day === undefined && c.lux_day !== before.lux_day)
            setNotice(`Ngưỡng về ngày đã được chỉnh thành ${c.lux_day} lux để cách ngưỡng đêm ít nhất 20 lux`)
          else if (want.lux_day !== undefined && want.lux_night === undefined && c.lux_night !== before.lux_night)
            setNotice(`Ngưỡng chuyển đêm đã được chỉnh thành ${c.lux_night} lux để cách ngưỡng ngày ít nhất 20 lux`)
          else if (Object.entries(want).some(([k, v]) => c[k as keyof Config] !== v)) setNotice("Board đã chỉnh giá trị về trong giới hạn cho phép")
          addLog("Board đã áp dụng cài đặt mới", "ok")
        }
      } else if (topic === T.history) {
        const h = json() as HistoryPayload | null
        if (h && Array.isArray(h.lamps)) setHistory24(h)
      } else if (topic === T.alert) {
        const a = text ? (json() as AlertPayload | null) : null
        setAlert(a)
        const id = a ? String(a.id) : null
        if (a && id !== prev.current.alertId) {
          const what = a.type === "vat_dung" ? `${CLASS_TEXT[a.class ?? ""] ?? "Vật"} dừng lâu ở vùng ${a.zone}` : (a.msg ?? ALERT_TEXT[a.type] ?? a.type)
          addLog(`Cảnh báo: ${what}`, "bad")
        }
        if (!a && prev.current.alertId) addLog("Cảnh báo đã đóng", "ok")
        prev.current.alertId = id
      } else if (topic === T.image) {
        setImage(text ? (text.startsWith("data:") ? text : `data:image/jpeg;base64,${text}`) : null)
      }
    },
    [addLog, markOnline],
  )

  const reset = useCallback(() => {
    client.current?.end(true)
    client.current = null
    sim.current?.stop()
    sim.current = null
    model.current = createLampModel()
    prev.current = { fault: [0, 0, 0, 0], night: null, mode: null, alertId: null, online: null }
    lastSeen.current = 0
    cfgRef.current = null
    setBoardOnline(null)
    setLastAt(null)
    setState(null)
    setConfig(null)
    setLamps(null)
    setHistory24(null)
    setAlert(null)
    setImage(null)
    setCurrent([])
    setLux([])
    setSource(null)
  }, [])

  const startSim = useCallback(() => {
    reset()
    setSource("sim")
    setStatus("connected")
    setError(null)
    addLog("Đang xem dữ liệu giả lập", "info")
    sim.current = createSimBoard((topic, payload) => onMessage(topic, payload))
  }, [reset, addLog, onMessage])

  const connect = useCallback(
    async (l: Login) => {
      reset()
      setSource("mqtt")
      setStatus("connecting")
      setError(null)
      const { default: mqtt } = await import("mqtt")
      // Nhập tên miền broker là đủ; nhập cả URL ws:// hoặc wss:// (vd broker thử trong mạng LAN) thì dùng nguyên URL đó
      const raw = l.host.trim()
      const full = /^wss?:\/\//.test(raw)
      const host = raw.replace(/^(wss?|mqtts?|https?):\/\//, "").replace(/[:/].*$/, "")
      const c = mqtt.connect(full ? raw : `wss://${host}:${l.port || 8084}/mqtt`, {
        username: l.username,
        password: l.password,
        clientId: `dashboard-${Math.random().toString(16).slice(2, 10)}`,
        reconnectPeriod: 5000,
        connectTimeout: 10_000,
        clean: true,
      })
      client.current = c
      let everConnected = false
      c.on("connect", () => {
        everConnected = true
        setStatus("connected")
        setError(null)
        addLog(`Đã kết nối broker MQTT (${host})`, "ok")
        c.subscribe([`${T.online}`, T.state, T.config, T.energy, T.history, T.alert, T.image])
      })
      c.on("message", (topic, buf) => onMessage(topic, buf.toString()))
      c.on("error", (e) => {
        const msg = mqttError(e as Error & { code?: number })
        setError(msg)
        if (!everConnected || /mật khẩu/.test(msg)) {
          setStatus("error")
          c.end(true)
        }
      })
      c.on("offline", () => everConnected && setStatus("connecting"))
      c.on("reconnect", () => everConnected && setStatus("connecting"))
    },
    [reset, addLog, onMessage],
  )

  const disconnect = useCallback(() => {
    reset()
    setStatus("idle")
    setError(null)
  }, [reset])

  // Board im lặng quá lâu thì báo mất kết nối
  useEffect(() => {
    const t = setInterval(() => {
      if (lastSeen.current && Date.now() - lastSeen.current > OFFLINE_MS) markOnline(false)
    }, 1000)
    return () => clearInterval(t)
  }, [markOnline])
  useEffect(() => () => reset(), [reset])

  /** Gửi lệnh tới board */
  const send = useCallback(
    (topic: string, payload: unknown, logText?: string) => {
      const text = typeof payload === "string" ? payload : JSON.stringify(payload)
      if (sim.current) sim.current.handle(topic, text)
      else if (client.current?.connected) client.current.publish(topic, text, { qos: 1 })
      else {
        addLog("Chưa kết nối, không gửi được lệnh", "bad")
        return
      }
      if (logText) addLog(logText, "info")
    },
    [addLog],
  )

  const cmd = {
    setMode: (m: "auto" | "manual") => send(T.cmdMode, m, `Đã gửi lệnh chuyển sang chế độ ${MODE_TEXT[m].toLowerCase()}`),
    setLamp: (lamp: number, duty: number) => send(T.cmdLamp, { lamp, duty: Math.round(duty) }, `Đã gửi lệnh đèn ${lamp} sáng ${Math.round(duty)}%`),
    setForceNight: (v: boolean) => send(T.cmdForceNight, v ? "true" : "false", v ? "Đã gửi lệnh ép chế độ đêm" : "Đã tắt ép chế độ đêm"),
    ack: (id: string | number) => send(T.cmdAck, { id }, "Đã đánh dấu cảnh báo là đã xử lý"),
    setConfig: (patch: Partial<Config>) => {
      pendingCfg.current = patch
      setNotice(null)
      send(T.cmdConfig, patch, `Đã gửi cài đặt: ${Object.entries(patch).map(([k, v]) => `${k} = ${v}`).join(", ")}`)
    },
    resetConfig: () => {
      pendingCfg.current = null
      setNotice(null)
      send(T.cmdConfig, { reset: true }, "Đã gửi lệnh khôi phục cài đặt mặc định")
    },
  }

  const clearLog = useCallback(() => {
    setLog([])
    saveLog([])
  }, [])

  return {
    status, error, source, boardOnline, lastAt, state, config, lamps, history24, alert, image, current, lux, log, notice,
    connect, startSim, disconnect, cmd, clearLog, dismissNotice: () => setNotice(null), sim,
    alertSince: alert?.since ? toMs(alert.since) : undefined,
  }
}
export type Station = ReturnType<typeof useStation>
