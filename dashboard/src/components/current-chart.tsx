import { CartesianGrid, Line, LineChart, ResponsiveContainer, Tooltip, XAxis, YAxis } from "recharts"
import type { SeriesPoint } from "@/lib/station"
import { clock, fmt } from "@/lib/format"
import { useMemo } from "react"

const COLORS = ["var(--color-chart-1)", "var(--color-chart-2)", "var(--color-chart-3)", "var(--color-chart-4)"]

export function CurrentChart({ history, stale }: { history: SeriesPoint[]; stale: boolean }) {
  // 120 lần đo gần nhất (giả lập gửi mỗi 1 giây, ESP32 thật mỗi 5 giây)
  const data = useMemo(
    () =>
      history.slice(-120).map((h) => ({
        time: clock(h.t),
        i1: h.v[0],
        i2: h.v[1],
        i3: h.v[2],
        i4: h.v[3],
      })),
    [history],
  )

  if (data.length === 0) {
    return <div className="h-[250px] flex items-center justify-center text-muted-foreground font-medium text-sm">Chưa có dữ liệu đồ thị</div>
  }

  return (
    <div className={`h-[250px] w-full p-4 transition-opacity duration-1000 ${stale ? "opacity-40" : "opacity-100"}`}>
      <ResponsiveContainer width="100%" height="100%">
        <LineChart data={data} margin={{ top: 10, right: 10, left: -20, bottom: 0 }}>
          <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="currentColor" className="opacity-10" />
          <XAxis 
            dataKey="time" 
            tick={{ fontSize: 11, fill: "var(--color-muted-foreground)" }}
            tickMargin={10}
            minTickGap={30}
            axisLine={false}
            tickLine={false}
          />
          <YAxis 
            tick={{ fontSize: 11, fill: "var(--color-muted-foreground)" }}
            axisLine={false}
            tickLine={false}
            tickFormatter={(v) => `${v}`}
          />
          <Tooltip 
            contentStyle={{ 
              backgroundColor: "var(--color-popover)",
              borderColor: "var(--color-border)",
              borderRadius: "0.75rem",
              boxShadow: "0 10px 15px -3px rgba(0, 0, 0, 0.1)",
              color: "var(--color-popover-foreground)",
              fontWeight: 600,
              fontSize: "12px",
              padding: "8px 12px"
            }}
            itemStyle={{ fontSize: "13px", fontWeight: 700 }}
            formatter={(value) => [`${fmt(Number(value), 1)} mA`]}
            labelStyle={{ color: "var(--color-muted-foreground)", marginBottom: "4px" }}
          />
          {[1, 2, 3, 4].map((id, i) => (
            <Line
              key={id}
              type="monotone"
              dataKey={`i${id}`}
              name={`Đèn ${id}`}
              stroke={COLORS[i]}
              strokeWidth={3}
              dot={false}
              activeDot={{ r: 6, strokeWidth: 0, fill: COLORS[i] }}
              isAnimationActive={false}
            />
          ))}
        </LineChart>
      </ResponsiveContainer>
    </div>
  )
}
