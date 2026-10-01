// Định dạng dữ liệu MQTT giữa ESP32 và web, theo bảng topic trong plans/00 của repo smart_street_lighting.
// Web chỉ đọc/ghi các topic này, không chứa mật khẩu.

export const PREFIX = "streetlight"
export const T = {
  online: `${PREFIX}/online`, // "online" / "offline" (LWT), retained
  state: `${PREFIX}/state`, // StatePayload, retained
  config: `${PREFIX}/config`, // Config, retained
  energy: `${PREFIX}/energy`, // EnergyPayload, retained
  history: `${PREFIX}/energy/history`, // HistoryPayload, retained
  alert: `${PREFIX}/alert`, // AlertPayload hoặc rỗng khi đã đóng, retained
  image: `${PREFIX}/alert/image`, // JPEG base64 hoặc rỗng, retained
  cmdMode: `${PREFIX}/cmd/mode`, // "auto" / "manual"
  cmdLamp: `${PREFIX}/cmd/lamp`, // {"lamp":3,"duty":60}
  cmdForceNight: `${PREFIX}/cmd/force_night`, // "true" / "false"
  cmdAck: `${PREFIX}/cmd/alert_ack`, // {"id":...}
  cmdConfig: `${PREFIX}/cmd/config`, // chỉ các tham số muốn đổi, hoặc {"reset":true}
} as const

export type Mode = "auto" | "manual" | "schedule"
export const MODE_TEXT: Record<string, string> = { auto: "Tự động", manual: "Chỉnh tay", schedule: "Theo giờ" }

export type StatePayload = {
  night?: boolean
  lux?: number
  mode?: Mode
  force_night?: boolean
  duty?: number[] // độ sáng 4 đèn, %
  persons?: number
  vehicles?: number
  zones?: number[] // các vùng đang có người/xe (1..4)
  fps?: number
  sensor_error?: string // "bh1750" / "camera" khi đang chạy chế độ theo giờ
}

export type Config = {
  lux_night: number
  lux_day: number
  fade_up_ms: number
  fade_down_ms: number
  night_start: string // "HH:MM"
  night_end: string
}
export const DEFAULT_CONFIG: Config = { lux_night: 30, lux_day: 100, fade_up_ms: 200, fade_down_ms: 3000, night_start: "18:00", night_end: "06:00" }
export const LIMITS = {
  lux: { min: 0, max: 1000, step: 5 },
  fade_up_ms: { min: 0, max: 2000, step: 50 },
  fade_down_ms: { min: 0, max: 10000, step: 250 },
  LUX_MIN_GAP: 20,
}

export type RawLamp = {
  id?: number
  duty?: number
  on?: boolean
  v?: number | null
  i?: number | null
  p?: number
  e_today?: number
  e_total?: number
  saving?: number
  fault?: number
  msg?: string
  ina_ok?: boolean
}
export type EnergyPayload = { lamps: RawLamp[] }
/** 24 giá trị điện năng (mWh) mỗi bóng, cũ nhất trước, giờ hiện tại cuối cùng. `t` = thời điểm giờ cuối (ms hoặc giây Unix) */
export type HistoryPayload = { t?: number; lamps: number[][] }
export type AlertPayload = {
  id: string | number
  type: string // "vat_dung" | "camera" | "bh1750" | "mat_gio" | "ina219"
  zone?: number
  class?: string // "nguoi" / "xe"
  since?: number // ms hoặc giây Unix
  status?: string
  msg?: string
}
export const ALERT_TEXT: Record<string, string> = {
  vat_dung: "Vật dừng lâu trên đường",
  camera: "Camera/AI lỗi, đang chạy chế độ theo giờ",
  bh1750: "Cảm biến ánh sáng BH1750 lỗi, đang chạy chế độ theo giờ",
  mat_gio: "Board khởi động lại chưa có giờ, đang sáng 100%",
  ina219: "Không đo được điện năng",
}
export const CLASS_TEXT: Record<string, string> = { nguoi: "Người", xe: "Xe" }

/* ---------------- Phát hiện đèn hỏng từ số đo INA219 ---------------- */

export const I100_MA = 20 // dòng một bóng ở 100% (mA)
const SHORT_MA = 100
const LOW_V = 4
const CONFIRM = 2

export const FAULT_TEXT: Record<number, string> = {
  1: "Hở mạch hoặc cháy bóng: đèn đang bật nhưng không có dòng",
  2: "Quá dòng hoặc chập mạch",
  3: "Đèn đã tắt nhưng vẫn có dòng: kiểm tra ULN2003",
  4: "Mất nguồn 5 V cấp cho đèn",
  5: "Không đọc được INA219 của bóng này",
}
export const FAULT_SHORT: Record<number, string> = { 1: "Đứt bóng", 2: "Chập mạch", 3: "Kẹt ULN2003", 4: "Mất nguồn", 5: "Lỗi INA219" }

export type Lamp = {
  id: number
  duty: number
  v: number | null
  i: number | null
  p: number // mW
  eToday: number // mWh
  eTotal: number // mWh
  saving: number | null // %
  fault: number
  msg: string
}

const num = (x: unknown) => (x === null || x === undefined || x === "" || !Number.isFinite(Number(x)) ? null : Number(x))
const clamp = (x: number, a: number, b: number) => Math.max(a, Math.min(b, x))

function detectFault(l: Lamp, prev: number) {
  if (l.v === null || l.i === null) return 5
  if (l.v < LOW_V) return 4
  if (l.i > SHORT_MA) return 2
  if (l.duty >= 10) return l.i < 0.3 * I100_MA * (l.duty / 100) ? 1 : 0
  if (l.duty === 0 && l.i > 2) return 3
  return prev === 1 ? 1 : 0 // đèn tắt thì không biết bóng còn tốt không: giữ lỗi đứt bóng tới khi đèn sáng lại có dòng
}

/** Chuẩn hoá gói streetlight/energy và phát hiện lỗi (khi ESP32 không gửi mã lỗi: lỗi lặp lại 2 lần mới báo) */
export function createLampModel() {
  const lamps: (Lamp & { pending: number; count: number })[] = [1, 2, 3, 4].map((id) => ({
    id, duty: 0, v: null, i: null, p: 0, eToday: 0, eTotal: 0, saving: null, fault: 0, msg: "", pending: 0, count: 0,
  }))
  return {
    setDuty(duty: number[]) {
      duty.slice(0, 4).forEach((d, k) => num(d) !== null && (lamps[k].duty = clamp(Number(d), 0, 100)))
    },
    ingest(raw: EnergyPayload): Lamp[] {
      ;(raw.lamps ?? []).slice(0, 4).forEach((r, k) => {
        const L = lamps[(num(r.id) ?? k + 1) - 1] ?? lamps[k]
        if (num(r.duty) !== null) L.duty = clamp(Number(r.duty), 0, 100)
        else if (typeof r.on === "boolean") L.duty = r.on ? 100 : 0
        L.v = num(r.v)
        L.i = num(r.i)
        if (r.ina_ok === false) L.v = L.i = null
        L.p = num(r.p) ?? (L.v !== null && L.i !== null ? L.v * L.i : 0)
        L.eToday = num(r.e_today) ?? L.eToday
        L.eTotal = num(r.e_total) ?? L.eTotal
        L.saving = num(r.saving)
        const fw = num(r.fault)
        const f = fw !== null ? clamp(fw | 0, 0, 9) : detectFault(L, L.fault)
        if (fw !== null || f === L.fault) {
          L.fault = f
          L.count = 0
        } else {
          if (f !== L.pending) Object.assign(L, { pending: f, count: 0 })
          if (++L.count >= CONFIRM) Object.assign(L, { fault: f, count: 0 })
        }
        L.msg = L.fault ? (r.msg || FAULT_TEXT[L.fault] || `Mã lỗi ${L.fault}`) : ""
      })
      return lamps.map(({ pending: _p, count: _c, ...l }) => l)
    },
  }
}

/** Kiểm tra lệnh cài đặt giống như board sẽ làm: kẹp giới hạn, giữ khoảng cách 2 ngưỡng lux */
export function applyConfig(cur: Config, patch: Partial<Config> & { reset?: boolean }): Config {
  if (patch.reset) return { ...DEFAULT_CONFIG }
  const c = { ...cur }
  const n = (k: keyof typeof LIMITS, v: unknown) => {
    const L = LIMITS[k] as { min: number; max: number }
    return num(v) === null ? null : clamp(Number(v), L.min, L.max)
  }
  if (patch.lux_night !== undefined) c.lux_night = n("lux", patch.lux_night) ?? c.lux_night
  if (patch.lux_day !== undefined) c.lux_day = n("lux", patch.lux_day) ?? c.lux_day
  if (patch.fade_up_ms !== undefined) c.fade_up_ms = n("fade_up_ms", patch.fade_up_ms) ?? c.fade_up_ms
  if (patch.fade_down_ms !== undefined) c.fade_down_ms = n("fade_down_ms", patch.fade_down_ms) ?? c.fade_down_ms
  const hhmm = /^([01]\d|2[0-3]):[0-5]\d$/
  if (patch.night_start && hhmm.test(patch.night_start)) c.night_start = patch.night_start
  if (patch.night_end && hhmm.test(patch.night_end)) c.night_end = patch.night_end
  if (c.night_start === c.night_end) c.night_end = cur.night_end === c.night_start ? DEFAULT_CONFIG.night_end : cur.night_end
  if (c.lux_day - c.lux_night < LIMITS.LUX_MIN_GAP) {
    if (patch.lux_night !== undefined && patch.lux_day === undefined) c.lux_day = Math.min(1000, c.lux_night + LIMITS.LUX_MIN_GAP)
    else c.lux_night = Math.max(0, c.lux_day - LIMITS.LUX_MIN_GAP)
    if (c.lux_day - c.lux_night < LIMITS.LUX_MIN_GAP) c.lux_day = c.lux_night + LIMITS.LUX_MIN_GAP
  }
  return c
}

export const toMs = (t: number | undefined) => (t === undefined ? undefined : t < 1e12 ? t * 1000 : t)
