// Bộ giả lập board ESP32-S3 chạy ngay trong trình duyệt, dùng khi chưa có phần cứng.
// Nó publish/nhận đúng các topic MQTT như firmware thật (plans/00, plans/01),
// nên web xử lý dữ liệu giả lập và dữ liệu thật bằng cùng một đoạn code.
import { applyConfig, DEFAULT_CONFIG, T, type AlertPayload, type Config, type Mode } from "@/lib/protocol"

type Publish = (topic: string, payload: string) => void
type Obj = { type: "nguoi" | "xe"; pos: number; dir: 1 | -1; speed: number; stopAt?: number; stopUntil?: number; stoppedSince?: number }

const TICK_MS = 1000 // board thật gửi mỗi 5 s (TELEMETRY_INTERVAL); giả lập gửi mỗi 1 s cho dễ xem
const I100 = [19.6, 20.3, 19.8, 20.1] // mA mỗi bóng ở 100%
const DARK_S = 150 // một vòng: 150 s phòng tối, 40 s bật đèn phòng
const BRIGHT_S = 40
const HOLD_DN = 10_000 // DAY_NIGHT_HOLD
const STOP_TIME = 10_000
const MANUAL_TIMEOUT = 10 * 60_000

/** Ảnh JPEG giả lập cho cảnh báo vật dừng: mặt đường 4 vùng, khung đỏ quanh vật */
function fakeImage(zone: number, cls: string): string {
  try {
    const c = document.createElement("canvas")
    c.width = 320
    c.height = 240
    const g = c.getContext("2d")!
    g.fillStyle = "#0b1220"
    g.fillRect(0, 0, 320, 240)
    g.fillStyle = "#1f2937"
    g.fillRect(0, 90, 320, 90)
    g.strokeStyle = "#9ca3af"
    g.setLineDash([14, 10])
    g.beginPath()
    g.moveTo(0, 135)
    g.lineTo(320, 135)
    g.stroke()
    g.setLineDash([3, 4])
    for (let k = 1; k < 4; k++) {
      g.beginPath()
      g.moveTo(k * 80, 90)
      g.lineTo(k * 80, 180)
      g.stroke()
    }
    g.setLineDash([])
    for (let k = 0; k < 4; k++) {
      const grd = g.createRadialGradient(k * 80 + 40, 60, 2, k * 80 + 40, 110, 70)
      grd.addColorStop(0, "rgba(244,173,61,.55)")
      grd.addColorStop(1, "rgba(244,173,61,0)")
      g.fillStyle = grd
      g.fillRect(k * 80, 40, 80, 150)
      g.fillStyle = "#e5e7eb"
      g.font = "bold 13px sans-serif"
      g.fillText(String(k + 1), k * 80 + 6, 172)
    }
    const x = (zone - 1) * 80 + 22
    g.fillStyle = cls === "xe" ? "#3b82f6" : "#f59e0b"
    if (cls === "xe") g.fillRect(x + 4, 120, 30, 16)
    else g.fillRect(x + 14, 108, 10, 26)
    g.strokeStyle = "#ef4444"
    g.lineWidth = 3
    g.strokeRect(x - 4, 100, 46, 44)
    g.fillStyle = "#ef4444"
    g.fillRect(x - 4, 84, 58, 16)
    g.fillStyle = "#fff"
    g.font = "bold 11px sans-serif"
    g.fillText(`${cls} 0.86`, x, 96)
    g.fillStyle = "rgba(255,255,255,.75)"
    g.font = "11px monospace"
    g.fillText(new Date().toLocaleString("vi-VN"), 8, 16)
    g.fillText("ANH GIA LAP", 230, 232)
    return c.toDataURL("image/jpeg", 0.7).split(",")[1]
  } catch {
    return ""
  }
}

export function createSimBoard(publish: Publish) {
  const t0 = Date.now()
  let config: Config = { ...DEFAULT_CONFIG }
  let mode: Mode = "auto"
  let manualUntil = 0
  const manualDuty = [20, 20, 20, 20]
  let forceNight = false
  let night = true
  let dnSince = 0 // thời điểm lux bắt đầu vượt ngưỡng (để giữ HOLD_DN)
  const duty = [20, 20, 20, 20]
  const holdUntil = [0, 0, 0, 0]
  const inject = [0, 0, 0, 0]
  const injectUntil = [0, 0, 0, 0]
  const objects: Obj[] = []
  let nextSpawn = Date.now() + 2000
  let nextStop = Date.now() + 25_000
  let nextFault = Date.now() + 45_000
  let alert: (AlertPayload & { obj?: Obj }) | null = null
  let alertSeq = 1
  // điện năng
  const eToday = [0, 0, 0, 0]
  const eTotal = [0, 0, 0, 0]
  const nightE = [0, 0, 0, 0]
  let nightMs = 0
  // 23 giờ trước: giả lập đêm qua đèn nền 20% có lúc sáng 100%, ban ngày tắt
  const hist: number[][] = I100.map((i100) =>
    Array.from({ length: 24 }, (_, k) => {
      const h = (new Date().getHours() - 23 + k + 24) % 24
      const isNight = h >= 18 || h < 6
      return k === 23 ? 0 : isNight ? +(5 * i100 * (0.2 + Math.random() * 0.12)).toFixed(1) : 0
    }),
  )
  hist.forEach((row, k) => (eTotal[k] = row.reduce((a, b) => a + b, 0) + 1500 + k * 40))
  let histHour = new Date().getHours()
  let last = Date.now()
  let lastHistPub = 0

  const pub = (topic: string, data: unknown) => publish(topic, typeof data === "string" ? data : JSON.stringify(data))
  const pubConfig = () => pub(T.config, config)

  function closeAlert() {
    if (!alert) return
    if (alert.obj) alert.obj.stopUntil = 0
    alert = null
    pub(T.alert, "")
    pub(T.image, "")
  }

  function tick() {
    const now = Date.now()
    const dt = Math.min((now - last) / 1000, 5)
    last = now

    // Ánh sáng môi trường và chế độ ngày/đêm có vùng trễ
    const cycle = ((now - t0) / 1000) % (DARK_S + BRIGHT_S)
    const lux = cycle < DARK_S ? 10 + Math.random() * 6 : 280 + Math.random() * 50
    const want = lux < config.lux_night ? true : lux > config.lux_day ? false : night
    if (want !== night) {
      dnSince ||= now
      if (now - dnSince >= HOLD_DN) {
        night = want
        dnSince = 0
      }
    } else dnSince = 0
    const isNight = forceNight || night

    // Người/xe đồ chơi
    if (isNight && now > nextSpawn) {
      const type = Math.random() < 0.55 ? "nguoi" : "xe"
      const dir = Math.random() < 0.5 ? 1 : -1
      const o: Obj = { type, dir, pos: dir > 0 ? 0 : 3.99, speed: type === "xe" ? 1.1 : 0.3 }
      if (!alert && now > nextStop) {
        o.stopAt = 1 + Math.floor(Math.random() * 2) + 0.5
        nextStop = now + 70_000 + Math.random() * 40_000
      }
      objects.push(o)
      nextSpawn = now + 7_000 + Math.random() * 9_000
    }
    for (const o of objects) {
      if (o.stopAt !== undefined && Math.abs(o.pos - o.stopAt) < 0.3 && o.stopUntil === undefined) {
        o.stopUntil = now + 35_000
        o.stoppedSince = now
      }
      if (o.stopUntil && now < o.stopUntil) continue
      o.pos += o.dir * o.speed * dt
    }
    for (let k = objects.length - 1; k >= 0; k--) {
      const o = objects[k]
      if (o.pos < 0 || o.pos >= 4 || (!isNight && !(o.stopUntil && now < o.stopUntil))) {
        if (alert?.obj === o) closeAlert()
        objects.splice(k, 1)
      }
    }
    // Cảnh báo vật dừng lâu
    for (const o of objects) {
      if (!alert && o.stopUntil && now < o.stopUntil && o.stoppedSince && now - o.stoppedSince > STOP_TIME) {
        const zone = Math.floor(o.pos) + 1
        alert = { id: alertSeq++, type: "vat_dung", zone, class: o.type, since: o.stoppedSince, status: "open", obj: o }
        const { obj: _o, ...payload } = alert
        pub(T.alert, payload)
        pub(T.image, fakeImage(zone, o.type))
      }
    }

    // Luật chiếu sáng
    for (const o of objects) {
      const z = Math.floor(o.pos)
      const stopped = o.stopUntil && now < o.stopUntil
      const zones = o.type === "nguoi" || stopped ? [z - 1, z, z + 1] : [z, z + o.dir]
      const hold = o.type === "nguoi" ? 5_000 : 2_000
      for (const k of zones) if (k >= 0 && k < 4) holdUntil[k] = Math.max(holdUntil[k], now + hold)
    }
    if (mode === "manual" && now > manualUntil) mode = "auto"
    const target = mode === "manual" ? [...manualDuty] : holdUntil.map((h) => (!isNight ? 0 : now < h ? 100 : 20))
    for (let k = 0; k < 4; k++) {
      const up = config.fade_up_ms ? (100 / config.fade_up_ms) * 1000 * dt : 100
      const down = config.fade_down_ms ? (100 / config.fade_down_ms) * 1000 * dt : 100
      duty[k] = target[k] > duty[k] ? Math.min(target[k], duty[k] + up) : Math.max(target[k], duty[k] - down)
    }

    // Bóng hỏng giả lập
    if (isNight && now > nextFault) {
      const free = [0, 1, 2, 3].filter((k) => !inject[k])
      const k = free[Math.floor(Math.random() * free.length)]
      if (k !== undefined) {
        inject[k] = Math.random() < 0.7 ? 1 : 2
        injectUntil[k] = now + 20_000
      }
      nextFault = now + 60_000 + Math.random() * 40_000
    }
    for (let k = 0; k < 4; k++) if (injectUntil[k] && now > injectUntil[k]) inject[k] = injectUntil[k] = 0

    // Số đo INA219 và điện năng
    const hour = new Date().getHours()
    if (hour !== histHour) {
      hist.forEach((row) => (row.shift(), row.push(0)))
      histHour = hour
      if (hour === 0) eToday.fill(0)
    }
    if (isNight) nightMs += dt * 1000
    const lamps = duty.map((d, k) => {
      const v = 5.02 + (Math.random() - 0.5) * 0.04
      let i = I100[k] * (d / 100) * (1 + (Math.random() - 0.5) * 0.02) + Math.random() * 0.08
      if (inject[k] === 1) i = Math.random() * 0.1
      if (inject[k] === 2) i = 140 + Math.random() * 20
      const p = v * i
      const dE = (p * dt) / 3600
      eToday[k] += dE
      eTotal[k] += dE
      hist[k][23] += dE
      if (isNight) nightE[k] += dE
      const ideal = (5 * I100[k] * nightMs) / 3_600_000
      const saving = nightMs > 10_000 ? Math.max(0, Math.min(100, Math.round((1 - nightE[k] / ideal) * 100))) : undefined
      return { id: k + 1, duty: Math.round(d), v: +v.toFixed(2), i: +i.toFixed(2), p: +p.toFixed(1), e_today: +eToday[k].toFixed(2), e_total: +eTotal[k].toFixed(1), saving }
    })
    const inZones = [...new Set(objects.map((o) => Math.floor(o.pos) + 1))].sort()
    pub(T.state, {
      night: isNight, lux: +lux.toFixed(1), mode, force_night: forceNight, duty: duty.map(Math.round),
      persons: objects.filter((o) => o.type === "nguoi").length, vehicles: objects.filter((o) => o.type === "xe").length,
      zones: inZones, fps: isNight ? +(9 + Math.random() * 2).toFixed(1) : 0,
    })
    pub(T.energy, { lamps })
    if (now - lastHistPub > 10_000) {
      pub(T.history, { t: now, lamps: hist.map((r) => r.map((x) => +x.toFixed(2))) })
      lastHistPub = now
    }
  }

  // Lệnh từ web (board kiểm tra mọi lệnh, sai thì bỏ qua)
  function handle(topic: string, payload: string) {
    let data: unknown = payload
    try {
      data = JSON.parse(payload)
    } catch {
      /* chuỗi thường như auto/manual */
    }
    if (topic === T.cmdMode && (data === "auto" || data === "manual")) {
      mode = data
      if (mode === "manual") {
        manualUntil = Date.now() + MANUAL_TIMEOUT
        duty.forEach((d, k) => (manualDuty[k] = Math.round(d)))
      }
    } else if (topic === T.cmdLamp && data && typeof data === "object") {
      const { lamp, duty: d } = data as { lamp: number; duty: number }
      if (mode === "manual" && Number.isInteger(lamp) && lamp >= 1 && lamp <= 4 && Number.isFinite(d) && d >= 0 && d <= 100) {
        manualDuty[lamp - 1] = Math.round(d)
        manualUntil = Date.now() + MANUAL_TIMEOUT
      }
    } else if (topic === T.cmdForceNight && typeof data === "boolean") forceNight = data
    else if (topic === T.cmdAck && data && typeof data === "object") {
      if (alert && String((data as { id: unknown }).id) === String(alert.id)) closeAlert()
    } else if (topic === T.cmdConfig && data && typeof data === "object") {
      config = applyConfig(config, data as Partial<Config> & { reset?: boolean })
      pubConfig()
    } else return
    tick()
  }

  pub(T.online, "online")
  pubConfig()
  pub(T.alert, "")
  pub(T.image, "")
  last = Date.now() - TICK_MS
  tick()
  pub(T.history, { t: Date.now(), lamps: hist })
  const timer = setInterval(tick, TICK_MS)

  return {
    handle,
    stop: () => clearInterval(timer),
    /** Nút trình diễn: cho 1 bóng hỏng / sửa xong */
    fault(id: number) {
      const k = id - 1
      inject[k] = (forceNight || night) && Math.random() < 0.7 ? 1 : 2
      injectUntil[k] = 0
    },
    repair(id: number) {
      inject[id - 1] = injectUntil[id - 1] = 0
    },
    /** Nút trình diễn: cho 1 xe dừng ngay để thấy cảnh báo kèm ảnh */
    stopNow() {
      if (alert) return
      const o: Obj = { type: "xe", dir: 1, pos: 2.5, speed: 0, stopAt: 2.5 }
      o.stopUntil = Date.now() + 40_000
      o.stoppedSince = Date.now() - STOP_TIME - 1
      objects.push(o)
      tick()
    },
  }
}
export type SimBoard = ReturnType<typeof createSimBoard>
