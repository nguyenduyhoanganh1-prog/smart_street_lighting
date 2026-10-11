import { useEffect, useState } from "react"
import { Info, RotateCcw, X } from "lucide-react"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { Slider } from "@/components/ui/slider"
import { fmt } from "@/lib/format"
import { LIMITS, type Config, type StatePayload } from "@/lib/protocol"
import type { Station } from "@/lib/station"

type NumKey = "lux_night" | "lux_day" | "fade_up_ms" | "fade_down_ms"

function SliderRow({ k, label, hint, unit, min, max, step, value, disabled, onCommit, extra }: {
  k: NumKey; label: string; hint: string; unit: string; min: number; max: number; step: number; value: number; disabled: boolean; onCommit: (k: NumKey, v: number) => void; extra?: React.ReactNode
}) {
  const [drag, setDrag] = useState<number | null>(null)
  useEffect(() => setDrag(null), [value])
  const shown = drag ?? value
  return (
    <div className="space-y-2.5">
      <div className="flex items-baseline justify-between gap-3">
        <Label htmlFor={k} className="font-semibold">{label}</Label>
        <span className="font-bold tnum">{unit === "s" ? `${fmt(shown / 1000, shown % 1000 ? 2 : 0)} s` : `${shown} ${unit}`}</span>
      </div>
      <Slider
        id={k}
        min={min}
        max={max}
        step={step}
        value={[shown]}
        disabled={disabled}
        onValueChange={([v]) => setDrag(v)}
        onValueCommit={([v]) => onCommit(k, v)} // chỉ gửi khi thả tay (sự kiện change), không gửi liên tục
        className="py-1.5 [&_[data-slot=slider-range]]:bg-frame [&_[data-slot=slider-thumb]]:size-5 [&_[data-slot=slider-thumb]]:border-frame"
      />
      <p className="text-xs text-muted-foreground">{hint}{extra}</p>
    </div>
  )
}

/** Cài đặt: ngưỡng lux, thời gian sáng dần/tối dần, giờ ban đêm. Luôn hiển thị theo streetlight/config (giá trị board đang dùng thật) */
export function SettingsPanel({ config, state, st, disabled }: { config: Config | null; state: StatePayload | null; st: Station; disabled: boolean }) {
  const off = disabled || !config
  const c = config ?? { lux_night: 30, lux_day: 100, fade_up_ms: 200, fade_down_ms: 3000, night_start: "18:00", night_end: "06:00" }
  const commit = (k: NumKey, v: number) => v !== c[k] && st.cmd.setConfig({ [k]: v })
  const [times, setTimes] = useState({ night_start: c.night_start, night_end: c.night_end })
  const commitTime = (
    key: "night_start" | "night_end",
  ) => {
    const value = times[key]

    if (!value || value === c[key]) return

    const other =
      key === "night_start"
        ? times.night_end
        : times.night_start

    // Không gửi cấu hình giờ bắt đầu = giờ kết thúc.
    if (value === other) return

    st.cmd.setConfig({
      [key]: value,
    })
  }
  useEffect(() => setTimes({ night_start: c.night_start, night_end: c.night_end }), [c.night_start, c.night_end])
  const luxNow = state?.lux != null ? <> Lux hiện tại: <b className="text-foreground">{fmt(state.lux, 0)} lux</b>.</> : null

  return (
    <div className="p-5 space-y-6">
      {st.notice && (
        <div role="status" className="flex items-start gap-2 rounded-xl border border-frame/30 bg-ok-soft px-3 py-2.5 text-sm font-medium">
          <Info className="mt-0.5 h-4 w-4 shrink-0 text-frame" />
          <span className="flex-1">{st.notice}</span>
          <button onClick={st.dismissNotice} aria-label="Đóng thông báo" className="text-muted-foreground hover:text-foreground"><X className="h-4 w-4" /></button>
        </div>
      )}
      {!config && <p className="text-sm text-muted-foreground">Đang chờ board gửi cài đặt hiện tại...</p>}

      <div className="grid gap-6 md:grid-cols-2">
        <SliderRow k="lux_night" label="Ngưỡng chuyển sang đêm" unit="lux" {...LIMITS.lux} value={c.lux_night} disabled={off} onCommit={commit}
          hint="Dưới mức này đủ lâu thì chuyển đêm, đèn nền 20%." extra={luxNow} />
        <SliderRow k="lux_day" label="Ngưỡng về ngày" unit="lux" {...LIMITS.lux} value={c.lux_day} disabled={off} onCommit={commit}
          hint={`Trên mức này đủ lâu thì về ngày, tắt đèn. Phải lớn hơn ngưỡng đêm ít nhất ${LIMITS.LUX_MIN_GAP} lux.`} />
        <SliderRow k="fade_up_ms" label="Thời gian sáng dần" unit="s" {...LIMITS.fade_up_ms} value={c.fade_up_ms} disabled={off} onCommit={commit}
          hint="Khi có người/xe, đèn tăng sáng trong khoảng này. Áp dụng từ lần chuyển tiếp theo." />
        <SliderRow k="fade_down_ms" label="Thời gian tối dần" unit="s" {...LIMITS.fade_down_ms} value={c.fade_down_ms} disabled={off} onCommit={commit}
          hint="Khi người/xe đi qua, đèn giảm sáng từ từ trong khoảng này." />
      </div>

      <div className="space-y-2">
        <p className="font-semibold">Giờ ban đêm (chế độ theo giờ)</p>
        <p className="text-xs text-muted-foreground">Chỉ dùng khi cảm biến ánh sáng hoặc camera lỗi: trong khoảng này đèn sáng 100%, ngoài khoảng này đèn tắt.</p>
        <div className="flex flex-wrap items-end gap-3">
          <div className="space-y-1">
            <Label htmlFor="ns" className="text-xs">Bắt đầu</Label>
            <Input
              id="ns"
              type="time"
              className="w-32 tnum"
              value={times.night_start}
              disabled={off}
              onChange={(e) =>
                setTimes({
                  ...times,
                  night_start: e.target.value,
                })
              }
              onBlur={() => commitTime("night_start")}
            />
          </div>
          <div className="space-y-1">
            <Label htmlFor="ne" className="text-xs">Kết thúc</Label>
            id="ne" type="time" className="w-32 tnum" value={times.night_end} disabled={off}
            <Input
              id="ne"
              type="time"
              className="w-32 tnum"
              value={times.night_end}
              disabled={off}
              onChange={(e) =>
                setTimes({
                  ...times,
                  night_end: e.target.value,
                })
              }
              onBlur={() => commitTime("night_end")}
            />
          </div>
          {times.night_start === times.night_end && <p className="text-xs text-destructive pb-2">Giờ bắt đầu và kết thúc không được trùng nhau</p>}
        </div>
      </div>

      <div className="flex justify-end border-t border-border pt-4">
        <Button variant="outline" disabled={off} onClick={() => st.cmd.resetConfig()}>
          <RotateCcw className="h-4 w-4" /> Khôi phục mặc định
        </Button>
      </div>
    </div>
  )
}
