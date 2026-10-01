/** Định dạng số kiểu Việt Nam: dấu phẩy thập phân */
export function fmt(x: number | null | undefined, digits = 2) {
  if (x == null || Number.isNaN(x)) return "–"
  return x.toLocaleString("vi-VN", { minimumFractionDigits: digits, maximumFractionDigits: digits })
}

export function time(d: Date) {
  return d.toLocaleTimeString("vi-VN", { hour12: false })
}

/** "vừa xong", "cách đây 12 giây", "cách đây 3 phút" */
export function formatRelativeTime(ms: number) {
  const s = Math.max(0, Math.round((Date.now() - ms) / 1000))
  if (s < 3) return "Vừa cập nhật"
  if (s < 60) return `Cập nhật cách đây ${s} giây`
  const m = Math.round(s / 60)
  if (m < 60) return `Cập nhật cách đây ${m} phút`
  return `Cập nhật lúc ${time(new Date(ms))}`
}

const pad = (n: number) => String(n).padStart(2, "0")
/** 01/10/2026 */
export function dateStr(ms: number) {
  const d = new Date(ms)
  return `${pad(d.getDate())}/${pad(d.getMonth() + 1)}/${d.getFullYear()}`
}
/** 05:24:43 */
export function clock(ms: number) {
  const d = new Date(ms)
  return `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`
}
/** 01/10/2026 05:24:43 */
export function dateTime(ms: number) {
  return `${dateStr(ms)} ${clock(ms)}`
}
const WEEKDAY = ["Chủ nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"]
/** Thứ Tư, 01/10/2026 */
export function longDate(ms: number) {
  return `${WEEKDAY[new Date(ms).getDay()]}, ${dateStr(ms)}`
}
/** "12 phút", "1 giờ 5 phút" */
export function duration(ms: number) {
  const s = Math.max(0, Math.round(ms / 1000))
  if (s < 60) return `${s} giây`
  const m = Math.floor(s / 60)
  if (m < 60) return `${m} phút ${s % 60} giây`
  return `${Math.floor(m / 60)} giờ ${m % 60} phút`
}
