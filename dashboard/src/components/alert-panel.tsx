import { useState } from "react"
import { Camera, CheckCircle2, Maximize2, TriangleAlert, X } from "lucide-react"
import { Button } from "@/components/ui/button"
import { dateTime, duration } from "@/lib/format"
import { ALERT_TEXT, CLASS_TEXT, type AlertPayload } from "@/lib/protocol"

/** Cảnh báo đang mở từ board (vật dừng lâu kèm ảnh, cảm biến lỗi...). Chỉ hiện khi có cảnh báo. */
export function AlertPanel({ alert, image, since, onAck }: { alert: AlertPayload; image: string | null; since?: number; onAck: () => void }) {
  const [zoom, setZoom] = useState(false)
  const stopped = alert.type === "vat_dung"
  const title = stopped ? `${CLASS_TEXT[alert.class ?? ""] ?? "Vật"} dừng lâu ở vùng ${alert.zone}` : (alert.msg ?? ALERT_TEXT[alert.type] ?? alert.type)

  return (
    <section aria-label="Cảnh báo" className="overflow-hidden rounded-2xl border-2 border-destructive bg-card shadow-lg shadow-destructive/15 animate-in zoom-in-95 duration-300">
      <div className={stopped ? "grid md:grid-cols-[minmax(0,22rem)_1fr]" : ""}>
        {image ? (
          <button onClick={() => setZoom(true)} className="group relative block bg-black" aria-label="Xem ảnh lớn">
            <img src={image} alt={`Ảnh camera lúc cảnh báo: ${title}`} className="aspect-[4/3] w-full object-cover" />
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
            {since && <div><dt className="text-muted-foreground">Bắt đầu lúc</dt><dd className="font-semibold tnum">{dateTime(since)}</dd></div>}
            {since && <div><dt className="text-muted-foreground">{stopped ? "Đã dừng" : "Kéo dài"}</dt><dd className="font-semibold tnum">{duration(Date.now() - since)}</dd></div>}
          </dl>
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
          <img src={image} alt={`Ảnh camera lúc cảnh báo: ${title}`} className="max-h-full max-w-full rounded-lg" />
          <button className="absolute right-4 top-4 rounded-full bg-white/15 p-2 text-white hover:bg-white/25" aria-label="Đóng"><X className="h-5 w-5" /></button>
        </div>
      )}
    </section>
  )
}
