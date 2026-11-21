import { useState } from "react"
import { Camera, CheckCircle2, Maximize2, TriangleAlert, X } from "lucide-react"
import { Button } from "@/components/ui/button"
import { dateTime, duration } from "@/lib/format"
import { ALERT_TEXT, CLASS_TEXT, toMs, type AlertObj, type AlertPayload } from "@/lib/protocol"

const DOT_COLOR: Record<string, string> = { xe: "#3b82f6", nguoi: "#f59e0b" }
const SAME_OBJ = 0.08 // chấm trong objs cách vị trí cảnh báo ít hơn mức này (0..1) là chính xe đang dừng

type Car = AlertObj & { no: number; alerted: boolean }

/** Các xe trên đường trong ảnh, đánh số 1, 2… từ trái sang phải. Board gửi objs (các xe của khung hình đó);
 *  chưa có thì chỉ xe đang cảnh báo */
function alertCars(alert: AlertPayload): Car[] {
  const list: AlertObj[] = alert.objs?.length
    ? alert.objs.filter((o) => o.zone !== 0).sort((a, b) => a.x - b.x) // zone 0 = ngoài đường: không chấm, không đánh số
    : alert.x != null && alert.y != null ? [{ x: alert.x, y: alert.y, class: alert.class, score: alert.score, zone: alert.zone, since: alert.since }] : []
  const near = (o: AlertObj) => alert.x != null && alert.y != null && Math.hypot(o.x - alert.x, o.y - alert.y) < SAME_OBJ
  return list.map((o, k) => ({ ...o, no: k + 1, alerted: near(o) }))
}

/** Chỉ chấm có số, không chữ: số của chấm = số trong danh sách bên cạnh; xe đang bị cảnh báo có vòng đỏ */
function CarDots({ cars, big = false }: { cars: Car[]; big?: boolean }) {
  return (
    <>
      {cars.map((c) => (
        <span key={c.no} className="pointer-events-none absolute" style={{ left: `${c.x * 100}%`, top: `${c.y * 100}%` }}>
          {c.alerted && <span className={`absolute -translate-x-1/2 -translate-y-1/2 rounded-full border-[3px] border-red-500 ${big ? "h-12 w-12" : "h-8 w-8"}`} />}
          <span className={`absolute flex -translate-x-1/2 -translate-y-1/2 items-center justify-center rounded-full border-2 border-white font-bold text-white shadow ${big ? "h-8 w-8 text-base" : "h-5 w-5 text-[11px]"}`}
            style={{ background: DOT_COLOR[c.class ?? ""] ?? "#ef4444" }}>
            {c.no}
          </span>
        </span>
      ))}
    </>
  )
}

/** Danh sách bên cạnh ảnh: xe số mấy, vùng nào, đã dừng bao lâu (đếm tiếp theo giờ thật) */
function CarList({ cars }: { cars: Car[] }) {
  if (cars.length === 0) return null
  return (
    <div className="space-y-1.5">
      <p className="text-sm text-muted-foreground">Xe trong ảnh ({cars.length})</p>
      <ul className="space-y-1">
        {cars.map((c) => (
          <li key={c.no} className={`flex flex-wrap items-center gap-x-2 rounded-md px-2 py-1 text-sm ${c.alerted ? "bg-bad-soft/50 font-semibold" : ""}`}>
            <span className="flex h-5 w-5 items-center justify-center rounded-full text-[11px] font-bold text-white" style={{ background: DOT_COLOR[c.class ?? ""] ?? "#ef4444" }}>{c.no}</span>
            <span>Xe {c.no}</span>
            <span className="text-muted-foreground">·</span>
            <span>Vùng {c.zone ?? "–"}</span>
            <span className="text-muted-foreground">·</span>
            <span className="tnum">{c.since ? `đã dừng ${duration(Date.now() - (toMs(c.since) ?? Date.now()))}` : "đang theo dõi"}</span>
            {c.alerted && <span className="text-destructive">· đang cảnh báo</span>}
          </li>
        ))}
      </ul>
    </div>
  )
}

/** Cảnh báo đang mở từ board (vật dừng lâu kèm ảnh, cảm biến lỗi...). Chỉ hiện khi có cảnh báo. */
export function AlertPanel({ alert, image, since, onAck }: { alert: AlertPayload; image: string | null; since?: number; onAck: () => void }) {
  const [zoom, setZoom] = useState(false)
  const stopped = alert.type === "vat_dung"
  const cars = stopped ? alertCars(alert) : []
  const alertedNo = cars.find((c) => c.alerted)?.no
  const title = stopped
    ? `${CLASS_TEXT[alert.class ?? ""] ?? "Vật"}${alertedNo ? ` số ${alertedNo}` : ""} dừng lâu ở vùng ${alert.zone}`
    : (alert.msg ?? ALERT_TEXT[alert.type] ?? alert.type)

  return (
    <section aria-label="Cảnh báo" className="overflow-hidden rounded-2xl border-2 border-destructive bg-card shadow-lg shadow-destructive/15 animate-in zoom-in-95 duration-300">
      <div className={stopped ? "grid md:grid-cols-[minmax(0,22rem)_1fr]" : ""}>
        {image ? (
          <button onClick={() => setZoom(true)} className="group relative block bg-black" aria-label="Xem ảnh lớn">
            <img src={image} alt={`Ảnh camera lúc cảnh báo: ${title}`} className="aspect-[4/3] w-full object-cover" />
            <CarDots cars={cars} />
            <span className="absolute bottom-2 right-2 flex items-center gap-1 rounded-md bg-black/60 px-2 py-1 text-xs text-white opacity-80 group-hover:opacity-100">
              <Maximize2 className="h-3.5 w-3.5" /> Phóng to
            </span>
          </button>
        ) : stopped ? (
          <div className="flex aspect-[4/3] items-center justify-center bg-muted text-sm text-muted-foreground"><Camera className="mr-2 h-4 w-4" /> Đang tải ảnh...</div>
        ) : null}
        <div className="flex flex-col gap-3 p-5">
          <p className="flex items-center gap-2 text-sm font-bold uppercase tracking-wide text-destructive">
            <TriangleAlert className="h-4 w-4 animate-pulse" /> Cảnh báo
          </p>
          <h2 className="text-2xl font-bold leading-tight">{title}</h2>
          <dl className="grid grid-cols-2 sm:grid-cols-4 md:grid-cols-2 gap-3 text-sm">
            {stopped && <div><dt className="text-muted-foreground">Vùng</dt><dd className="font-semibold">Vùng {alert.zone}</dd></div>}
            {stopped && <div><dt className="text-muted-foreground">Loại</dt><dd className="font-semibold">{CLASS_TEXT[alert.class ?? ""] ?? alert.class ?? "–"}</dd></div>}
            {stopped && alert.score != null && <div><dt className="text-muted-foreground">Độ tin cậy AI</dt><dd className="font-semibold tnum">{alert.score}%</dd></div>}
            {since && <div><dt className="text-muted-foreground">Bắt đầu lúc</dt><dd className="font-semibold tnum">{dateTime(since)}</dd></div>}
            {since && <div><dt className="text-muted-foreground">{stopped ? "Đã dừng" : "Kéo dài"}</dt><dd className="font-semibold tnum">{duration(Date.now() - since)}</dd></div>}
          </dl>
          {stopped && <CarList cars={cars} />}
          {stopped && <p className="text-sm text-muted-foreground">Đèn quanh vùng này đang sáng 100%. Xem ảnh để đánh giá: xe hỏng, tai nạn hay chỉ dừng tạm.</p>}
          <div className="mt-auto pt-1">
            <Button onClick={onAck} className="bg-frame text-white hover:bg-frame/90">
              <CheckCircle2 className="h-4 w-4" /> Đã xử lý
            </Button>
          </div>
        </div>
      </div>
      {zoom && image && (
        <div role="dialog" aria-modal="true" aria-label="Ảnh cảnh báo" className="fixed inset-0 z-[100] flex items-center justify-center bg-black/85 p-4" onClick={() => setZoom(false)}>
          <div className="relative inline-block max-h-full max-w-full">
            <img src={image} alt={`Ảnh camera lúc cảnh báo: ${title}`} className="block max-h-[90vh] max-w-full rounded-lg" />
            <CarDots cars={cars} big />
          </div>
          <button className="absolute right-4 top-4 rounded-full bg-white/15 p-2 text-white hover:bg-white/25" aria-label="Đóng"><X className="h-5 w-5" /></button>
        </div>
      )}
    </section>
  )
}
