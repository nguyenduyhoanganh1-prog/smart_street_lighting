import { useMemo } from "react"
import { Area, AreaChart, CartesianGrid, ReferenceLine, ResponsiveContainer, Tooltip, XAxis, YAxis } from "recharts"
import { Moon, Sun } from "lucide-react"
import { clock, fmt } from "@/lib/format"
import type { Config, StatePayload } from "@/lib/protocol"
import type { SeriesPoint } from "@/lib/station"

/** Cường độ ánh sáng: lux hiện tại, vị trí so với 2 ngưỡng, biểu đồ vài phút gần nhất (vẽ trong lúc trang mở) */
export function LightPanel({ state, config, series }: { state: StatePayload | null; config: Config | null; series: SeriesPoint[] }) {
  const lux = state?.lux
  const night = state?.night
  const max = Math.max(150, (config?.lux_day ?? 100) * 1.6, (lux ?? 0) * 1.05)
  const pos = (v: number) => `${Math.min(100, (v / max) * 100)}%`
  const data = useMemo(() => series.map((p) => ({ t: clock(p.t), lux: p.v[0] })), [series])

  return (
    <div className="p-5 space-y-5">
      <div className="flex items-end justify-between gap-3">
        <div>
          <p className="text-sm font-semibold text-muted-foreground">Ánh sáng môi trường (BH1750)</p>
          <p className="text-5xl font-black tnum tracking-tight leading-none mt-1">
            {lux == null ? "–" : fmt(lux, 0)}
            <span className="ml-1.5 text-xl font-bold text-muted-foreground">lux</span>
          </p>
        </div>
        <span className={`inline-flex items-center gap-1.5 rounded-full px-3 py-1 text-sm font-bold ${night ? "bg-[#1d2b45] text-[#c9d6ff]" : "bg-accent text-accent-foreground"}`}>
          {night ? <Moon className="h-4 w-4" /> : <Sun className="h-4 w-4" />}
          {night == null ? "–" : night ? (state?.force_night ? "Đêm (ép)" : "Ban đêm") : "Ban ngày"}
        </span>
      </div>

      {/* Thanh lux với 2 ngưỡng */}
      <div>
        <div className="relative h-3 rounded-full bg-gradient-to-r from-[#1d2b45] via-[#6b7fa8] to-[#f4ad3d]">
          {config && (
            <>
              <div className="absolute -top-1 h-5 w-0.5 bg-foreground/70" style={{ left: pos(config.lux_night) }} />
              <div className="absolute -top-1 h-5 w-0.5 bg-foreground/70" style={{ left: pos(config.lux_day) }} />
            </>
          )}
          {lux != null && <div className="absolute top-1/2 h-5 w-5 -translate-x-1/2 -translate-y-1/2 rounded-full border-2 border-background bg-frame shadow transition-[left] duration-700" style={{ left: pos(lux) }} aria-label={`Lux hiện tại ${fmt(lux, 0)}`} />}
        </div>
        {config && (
          <div className="relative mt-2 h-8 text-xs text-muted-foreground">
            <span className="absolute -translate-x-1/2 whitespace-nowrap text-center" style={{ left: pos(config.lux_night) }}>
              Đêm &lt; {config.lux_night}
            </span>
            <span className="absolute -translate-x-1/2 whitespace-nowrap text-center" style={{ left: pos(config.lux_day) }}>
              Ngày &gt; {config.lux_day}
            </span>
          </div>
        )}
      </div>

      <div className="h-36 -mx-2">
        {data.length < 2 ? (
          <div className="h-full flex items-center justify-center text-sm text-muted-foreground">Biểu đồ lux sẽ hiện sau vài giây</div>
        ) : (
          <ResponsiveContainer width="100%" height="100%">
            <AreaChart data={data} margin={{ top: 6, right: 8, left: -18, bottom: 0 }}>
              <defs>
                <linearGradient id="luxFill" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="0%" stopColor="var(--color-sodium)" stopOpacity={0.5} />
                  <stop offset="100%" stopColor="var(--color-sodium)" stopOpacity={0} />
                </linearGradient>
              </defs>
              <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="currentColor" className="opacity-10" />
              <XAxis dataKey="t" tick={{ fontSize: 10, fill: "var(--color-muted-foreground)" }} minTickGap={40} axisLine={false} tickLine={false} />
              <YAxis tick={{ fontSize: 10, fill: "var(--color-muted-foreground)" }} axisLine={false} tickLine={false} width={44} />
              <Tooltip formatter={(v) => [`${fmt(Number(v), 0)} lux`, "Ánh sáng"]} contentStyle={{ background: "var(--color-popover)", borderColor: "var(--color-border)", borderRadius: 10, fontSize: 12 }} />
              {config && <ReferenceLine y={config.lux_night} stroke="var(--color-chart-2)" strokeDasharray="4 4" />}
              {config && <ReferenceLine y={config.lux_day} stroke="var(--color-chart-1)" strokeDasharray="4 4" />}
              <Area type="monotone" dataKey="lux" stroke="var(--color-sodium)" strokeWidth={2} fill="url(#luxFill)" isAnimationActive={false} />
            </AreaChart>
          </ResponsiveContainer>
        )}
      </div>
    </div>
  )
}
