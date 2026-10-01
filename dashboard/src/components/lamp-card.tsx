import { CircleAlert, CircleOff, Lightbulb, Wrench, Zap, Activity } from "lucide-react"
import { Badge } from "@/components/ui/badge"
import { Card, CardContent, CardDescription, CardFooter, CardHeader, CardTitle } from "@/components/ui/card"
import { cn } from "@/lib/utils"
import { fmt } from "@/lib/format"
import { Slider } from "@/components/ui/slider"
import { useEffect, useState } from "react"
import { LAMP_NAMES } from "@/config"
import { FAULT_TEXT, type Lamp } from "@/lib/protocol"

type Props = {
  index: number // 1..4
  lamp: Lamp | undefined
  stale: boolean
  manual: boolean // đang ở chế độ chỉnh tay thì hiện thanh kéo độ sáng
  onDuty: (duty: number) => void
}

export function LampCard({ index, lamp, stale, manual, onDuty }: Props) {
  const L: Lamp = lamp ?? { id: index, duty: 0, v: null, i: null, p: 0, eToday: 0, eTotal: 0, saving: null, fault: 0, msg: "" }
  const isOk = L.duty > 0 && L.fault === 0
  const isFault = L.fault > 0
  // Giá trị đang kéo; thả tay mới gửi lệnh (tránh gửi hàng trăm tin MQTT)
  const [drag, setDrag] = useState<number | null>(null)
  useEffect(() => setDrag(null), [manual])

  // Animation cho border nếu đèn hỏng
  const borderClass = isFault 
    ? "border-destructive border-2 shadow-[0_0_15px_rgba(210,59,59,0.3)] animate-pulse" 
    : isOk 
      ? "border-ok/30 hover:border-ok/50 shadow-sm transition-all" 
      : "border-border/50 opacity-80"
  const staleClass = stale ? "opacity-60" : ""

  const bgClass = isFault ? "bg-bad-soft/30" : isOk ? "bg-card hover:bg-ok-soft/10 transition-colors" : "bg-muted/20"

  return (
    <Card className={cn("flex flex-col relative overflow-hidden group duration-300", borderClass, bgClass, staleClass)}>
      {/* Decorative background glow cho đèn đang bật */}
      {isOk && <div className="absolute -top-12 -right-12 w-32 h-32 bg-ok/10 rounded-full blur-3xl opacity-50 group-hover:opacity-100 transition-opacity"></div>}
      
      <CardHeader className="pb-3 flex flex-row items-start justify-between relative z-10">
        <div>
          <CardTitle className="flex items-center gap-2 text-lg font-bold">
            <span className="bg-frame text-white w-6 h-6 rounded flex items-center justify-center text-xs shadow-sm shadow-frame/40">
              {index}
            </span>
            Đèn {index}
          </CardTitle>
          <CardDescription className="mt-1 font-medium">{LAMP_NAMES[index - 1]} trên mô hình</CardDescription>
        </div>
        <div>
          {isFault ? (
            <Badge variant="destructive" className="gap-1.5 shadow-sm px-2.5 py-1 text-xs uppercase font-bold animate-in zoom-in">
              <Wrench className="h-3.5 w-3.5" /> Lỗi
            </Badge>
          ) : isOk ? (
            <Badge variant="outline" className="bg-ok-soft text-ok border-ok/20 gap-1.5 shadow-sm px-2.5 py-1 text-xs uppercase font-bold tnum">
              <Lightbulb className="h-3.5 w-3.5" /> Sáng {Math.round(L.duty)}%
            </Badge>
          ) : (
            <Badge variant="secondary" className="gap-1.5 shadow-sm px-2.5 py-1 text-xs uppercase font-bold">
              <CircleOff className="h-3.5 w-3.5" /> Tắt
            </Badge>
          )}
        </div>
      </CardHeader>
      
      <CardContent className="flex-1 pb-4 relative z-10">
        {/* Lưới thông số phong cách dashboard hiện đại */}
        <div className="grid grid-cols-2 gap-3 mb-4">
          <div className="bg-background rounded-xl p-3 border border-border/50 shadow-sm flex flex-col justify-center">
            <div className="flex items-center gap-1.5 text-xs text-muted-foreground font-semibold mb-1 uppercase tracking-wider">
              <Activity className="h-3.5 w-3.5 text-blue-500" /> Điện áp
            </div>
            <div className="text-xl font-bold tnum">
              {fmt(L.v, 2)} <span className="text-sm font-semibold text-muted-foreground">V</span>
            </div>
          </div>
          
          <div className="bg-background rounded-xl p-3 border border-border/50 shadow-sm flex flex-col justify-center">
            <div className="flex items-center gap-1.5 text-xs text-muted-foreground font-semibold mb-1 uppercase tracking-wider">
              <Zap className="h-3.5 w-3.5 text-yellow-500" /> Dòng điện
            </div>
            <div className={cn("text-xl font-bold tnum", L.fault === 2 && "text-destructive")}>
              {fmt(L.i, 1)} <span className="text-sm font-semibold text-muted-foreground">mA</span>
            </div>
          </div>
        </div>

        <div className="flex items-center justify-between px-1">
          <div className="text-sm font-semibold text-muted-foreground uppercase tracking-wider">Công suất</div>
          <div className="text-2xl font-black text-foreground tnum tracking-tight flex items-baseline gap-1">
            {fmt(L.p, 1)} <span className="text-base text-muted-foreground font-bold">mW</span>
          </div>
        </div>

        {/* Độ sáng PWM: 0% tắt, 20% nền, 100% sáng. Chế độ chỉnh tay thì kéo được */}
        <div className="mt-4 px-1">
          <div className="mb-2 flex items-center justify-between text-sm">
            <span className="font-semibold text-muted-foreground">{manual ? "Chỉnh độ sáng" : "Độ sáng"}</span>
            <span className="font-bold tnum">{Math.round(drag ?? L.duty)}%</span>
          </div>
          {manual ? (
            <Slider
              aria-label={`Độ sáng đèn ${index}`}
              min={0}
              max={100}
              step={5}
              value={[drag ?? L.duty]}
              onValueChange={([v]) => setDrag(v)}
              onValueCommit={([v]) => {
                onDuty(v)
                setTimeout(() => setDrag(null), 1500)
              }}
              className="py-2 [&_[data-slot=slider-range]]:bg-sodium [&_[data-slot=slider-thumb]]:size-5 [&_[data-slot=slider-thumb]]:border-sodium"
            />
          ) : (
            <div className="h-1.5 w-full rounded-full bg-muted overflow-hidden" aria-hidden>
              <div className={cn("h-full rounded-full transition-[width] duration-700", isFault ? "bg-destructive" : "bg-sodium")} style={{ width: `${L.duty}%` }} />
            </div>
          )}
        </div>

        <dl className="mt-4 grid grid-cols-2 gap-3 px-1 text-sm">
          <div>
            <dt className="text-muted-foreground font-medium">Điện năng hôm nay</dt>
            <dd className="font-bold tnum">{fmt(L.eToday, 1)} mWh</dd>
          </div>
          <div>
            <dt className="text-muted-foreground font-medium">Tiết kiệm</dt>
            <dd className="font-bold tnum text-ok">{L.saving == null ? "–" : `${L.saving}%`}</dd>
          </div>
        </dl>
      </CardContent>

      {isFault && (
        <CardFooter className="bg-destructive/10 border-t border-destructive/20 py-3 px-4 relative z-10">
          <div className="flex gap-2 text-sm font-bold text-destructive items-center w-full">
            <CircleAlert className="h-4 w-4 shrink-0" />
            <span>{L.msg || FAULT_TEXT[L.fault] || "Lỗi không xác định"}</span>
          </div>
        </CardFooter>
      )}
    </Card>
  )
}
