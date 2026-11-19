import { CheckCircle2, Loader2, ScanLine, TriangleAlert } from "lucide-react"
import { Button } from "@/components/ui/button"
import { clock, dateTime } from "@/lib/format"
import { toMs, type CalibPayload, type Line } from "@/lib/protocol"

const PENDING_MAX_MS = 60_000 // board không trả lời trong 1 phút thì thôi chờ
const ZONES = 4

/** Hình học giống road_zones.c / trang ai-stream, đơn vị pixel của ảnh calib */
function geometry(c: Required<Pick<CalibPayload, "top" | "bottom" | "x_min" | "x_max" | "bounds">> & CalibPayload) {
  const yOf = (l: Line, x: number) => l[0] * x + l[1]
  // x nơi đầu trái xiên (x = a*y + b) cắt đường y = a*x + b; không có đầu trái thì là x_min
  const leftEndOn = (l: Line) => (c.left ? (c.left[0] * l[1] + c.left[1]) / (1 - c.left[0] * l[0]) : c.x_min)
  const outerTop = (x: number) => (c.kerb_top ? Math.min(yOf(c.kerb_top, x), yOf(c.top, x)) : yOf(c.top, x))
  const outerBottom = (x: number) => (c.kerb_bottom ? Math.max(yOf(c.kerb_bottom, x), yOf(c.bottom, x)) : yOf(c.bottom, x))
  const xs = leftEndOn(c.top)
  const xe = leftEndOn(c.bottom)
  const road = [[xs, yOf(c.top, xs)], [c.x_max, yOf(c.top, c.x_max)], [c.x_max, yOf(c.bottom, c.x_max)], [xe, yOf(c.bottom, xe)]]
  const kerbs = [c.kerb_top, c.kerb_bottom].filter((l): l is Line => !!l).map((l) => {
    const x0 = Math.max(c.x_min, leftEndOn(l))
    return [x0, yOf(l, x0), c.x_max, yOf(l, c.x_max)]
  })
  const edges = [Math.max(xs, xe), ...c.bounds, c.x_max]
  const labels = Array.from({ length: ZONES }, (_, k) => {
    const mid = (edges[k] + edges[k + 1]) / 2
    return [mid, (yOf(c.top, mid) + yOf(c.bottom, mid)) / 2]
  })
  return { road, kerbs, outerTop, outerBottom, labels }
}

function Overlay({ calib }: { calib: CalibPayload }) {
  const { top, bottom, x_min, x_max, bounds } = calib
  if (calib.state !== "found" || !top || !bottom || x_min == null || x_max == null || !bounds) return null
  const g = geometry({ ...calib, top, bottom, x_min, x_max, bounds })
  return (
    <svg viewBox={`0 0 ${calib.w} ${calib.h}`} className="absolute inset-0 h-full w-full" aria-hidden="true">
      <polygon points={g.road.map((p) => p.join(",")).join(" ")} fill="rgba(34,197,94,0.12)" stroke="rgb(34,197,94)" strokeWidth="1.5" />
      {g.kerbs.map(([x1, y1, x2, y2], i) => (
        <line key={i} x1={x1} y1={y1} x2={x2} y2={y2} stroke="rgb(249,115,22)" strokeWidth="1.5" />
      ))}
      {[x_min, x_max].map((x) => (
        <line key={`end${x}`} x1={x} y1={g.outerTop(x)} x2={x} y2={g.outerBottom(x)} stroke="rgb(249,115,22)" strokeWidth="1" strokeDasharray="3 2" />
      ))}
      {bounds.map((b) => (
        <line key={b} x1={b} y1={g.outerTop(b)} x2={b} y2={g.outerBottom(b)} stroke="rgb(239,68,68)" strokeWidth="1.5" />
      ))}
      {g.labels.map(([x, y], k) => (
        <text key={k} x={x} y={y} textAnchor="middle" dominantBaseline="middle" fontSize="12" fontWeight="bold" fill="rgb(250,204,21)" stroke="black" strokeWidth="0.6" paintOrder="stroke">
          {k + 1}
        </text>
      ))}
    </svg>
  )
}

type Props = {
  calib: CalibPayload | null
  image: string | null
  receivedAt: number | null
  requestedAt: number | null
  now: number
  disabled: boolean
  onRecalibrate: () => void
}

/** Ảnh camera lúc tìm đường + đường/lề/vùng board tìm được, để kiểm tra bằng mắt; nút tìm lại đường */
export function CalibPanel({ calib, image, receivedAt, requestedAt, now, disabled, onRecalibrate }: Props) {
  const waiting = requestedAt !== null && (receivedAt === null || receivedAt < requestedAt)
  const pending = waiting && now - requestedAt < PENDING_MAX_MS
  const timedOut = waiting && !pending
  const found = calib?.state === "found"
  const at = toMs(calib?.t) ?? receivedAt ?? undefined

  return (
    <div className="space-y-3 p-4">
      <div className="relative aspect-[4/3] w-full overflow-hidden rounded-lg bg-black">
        {image ? (
          <img src={image} alt="Khung hình camera lúc tìm đường (4 đèn sáng tối đa)" className="absolute inset-0 h-full w-full object-fill" />
        ) : (
          <div className="absolute inset-0 flex items-center justify-center p-4 text-center text-sm text-white/60">
            {calib ? "Board không gửi kèm ảnh lần này" : "Chưa có ảnh. Board gửi ảnh sau mỗi lần tìm đường (lúc khởi động, đầu mỗi đêm, hoặc khi bấm nút bên dưới)."}
          </div>
        )}
        {calib && image && <Overlay calib={calib} />}
        {pending && (
          <div className="absolute inset-0 flex items-center justify-center gap-2 bg-black/55 text-sm font-semibold text-white">
            <Loader2 className="h-4 w-4 animate-spin" /> Đang tìm đường...
          </div>
        )}
      </div>

      <div className="flex flex-wrap items-center justify-between gap-3">
        <div className="min-w-0 text-sm">
          {!calib ? (
            <p className="text-muted-foreground">Chưa nhận được kết quả tìm đường</p>
          ) : found ? (
            <p className="flex items-center gap-1.5 font-semibold text-ok"><CheckCircle2 className="h-4 w-4 shrink-0" /> Đã tìm thấy đường</p>
          ) : (
            <p className="flex items-center gap-1.5 font-semibold text-destructive"><TriangleAlert className="h-4 w-4 shrink-0" /> Không tìm thấy đường: vùng chia 4 phần đều</p>
          )}
          {at && <p className="text-xs text-muted-foreground tnum">Lúc {now - at < 86_400_000 ? clock(at) : dateTime(at)}</p>}
        </div>
        <Button onClick={onRecalibrate} disabled={disabled || pending} variant="outline">
          {pending ? <Loader2 className="h-4 w-4 animate-spin" /> : <ScanLine className="h-4 w-4" />} Tìm lại đường
        </Button>
      </div>

      {timedOut && <p className="text-sm text-destructive">Board chưa gửi kết quả sau 1 phút. Kiểm tra camera hoặc kết nối của board.</p>}
      <p className="text-xs text-muted-foreground">
        <span className="font-semibold text-green-600 dark:text-green-400">Xanh</span>: mặt đường ·{" "}
        <span className="font-semibold text-orange-500">cam</span>: mép ngoài của lề ·{" "}
        <span className="font-semibold text-red-500">đỏ</span>: ranh giới 4 vùng (vùng k ứng với đèn k). Người/xe trong khung cam đều bật đèn.
        Khi tìm lại, 4 đèn sáng tối đa khoảng 2 giây.
      </p>
    </div>
  )
}
