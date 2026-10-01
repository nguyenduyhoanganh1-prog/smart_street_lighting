import { useMemo, useState } from "react"
import { Bar, BarChart, CartesianGrid, Legend, ResponsiveContainer, Tooltip, XAxis, YAxis } from "recharts"
import { Table, TableBody, TableCell, TableFooter, TableHead, TableHeader, TableRow } from "@/components/ui/table"
import { Tabs, TabsList, TabsTrigger } from "@/components/ui/tabs"
import { fmt } from "@/lib/format"
import { toMs, type HistoryPayload, type Lamp } from "@/lib/protocol"
import { cn } from "@/lib/utils"

const COLORS = ["var(--color-chart-1)", "var(--color-chart-2)", "var(--color-chart-3)", "var(--color-chart-4)"]
const sum = (ls: Lamp[], f: (l: Lamp) => number) => ls.reduce((a, l) => a + f(l), 0)

/** Bảng điện năng 4 bóng */
export function EnergyTable({ lamps }: { lamps: Lamp[] | null }) {
  const ls = lamps ?? []
  const savings = ls.map((l) => l.saving).filter((x): x is number => x != null)
  return (
    <Table className="tnum">
      <TableHeader>
        <TableRow>
          <TableHead>Bóng</TableHead>
          <TableHead className="text-right">Công suất (mW)</TableHead>
          <TableHead className="text-right">Dòng (mA)</TableHead>
          <TableHead className="text-right">Hôm nay (mWh)</TableHead>
          <TableHead className="text-right">Tổng (mWh)</TableHead>
          <TableHead className="text-right">Tiết kiệm</TableHead>
        </TableRow>
      </TableHeader>
      <TableBody>
        {[1, 2, 3, 4].map((id) => {
          const l = ls.find((x) => x.id === id)
          return (
            <TableRow key={id} className={cn(l?.fault && "bg-bad-soft/60 text-destructive")}>
              <TableCell className="font-semibold">
                <span className="mr-2 inline-block h-2.5 w-2.5 rounded-full" style={{ background: COLORS[id - 1] }} />
                Đèn {id}
              </TableCell>
              <TableCell className="text-right">{fmt(l?.p, 1)}</TableCell>
              <TableCell className="text-right">{fmt(l?.i, 1)}</TableCell>
              <TableCell className="text-right">{fmt(l?.eToday, 1)}</TableCell>
              <TableCell className="text-right">{fmt(l?.eTotal, 0)}</TableCell>
              <TableCell className="text-right text-ok font-semibold">{l?.saving == null ? "–" : `${l.saving}%`}</TableCell>
            </TableRow>
          )
        })}
      </TableBody>
      <TableFooter>
        <TableRow className="font-bold">
          <TableCell>Tổng cộng</TableCell>
          <TableCell className="text-right">{ls.length ? fmt(sum(ls, (l) => l.p), 1) : "–"}</TableCell>
          <TableCell className="text-right">{ls.length ? fmt(sum(ls, (l) => l.i ?? 0), 1) : "–"}</TableCell>
          <TableCell className="text-right">{ls.length ? fmt(sum(ls, (l) => l.eToday), 1) : "–"}</TableCell>
          <TableCell className="text-right">{ls.length ? fmt(sum(ls, (l) => l.eTotal), 0) : "–"}</TableCell>
          <TableCell className="text-right text-ok">{savings.length ? `${Math.round(savings.reduce((a, b) => a + b, 0) / savings.length)}%` : "–"}</TableCell>
        </TableRow>
      </TableFooter>
    </Table>
  )
}

/** Biểu đồ cột điện năng theo giờ trong 24 giờ gần nhất (topic streetlight/energy/history) */
export function Energy24h({ history }: { history: HistoryPayload | null }) {
  const [sel, setSel] = useState("all")
  const data = useMemo(() => {
    if (!history) return []
    const n = Math.max(...history.lamps.map((r) => r.length), 0)
    const endHour = new Date(toMs(history.t) ?? Date.now()).getHours()
    return Array.from({ length: n }, (_, k) => {
      const h = (endHour - (n - 1 - k) + 48) % 24
      const row: Record<string, number | string> = { h: `${h}h` }
      history.lamps.forEach((r, j) => (row[`l${j + 1}`] = r[k] ?? 0))
      return row
    })
  }, [history])
  const ids = sel === "all" ? [1, 2, 3, 4] : [+sel]

  return (
    <div className="space-y-3">
      <Tabs value={sel} onValueChange={setSel}>
        <TabsList className="grid h-auto w-full grid-cols-5 sm:w-auto sm:inline-grid">
          <TabsTrigger value="all" className="px-1 text-xs sm:px-3 sm:text-sm">Cả 4</TabsTrigger>
          {[1, 2, 3, 4].map((id) => (
            <TabsTrigger key={id} value={String(id)} className="px-1 text-xs sm:px-3 sm:text-sm">
              Đèn {id}
            </TabsTrigger>
          ))}
        </TabsList>
      </Tabs>
      <div className="h-64">
        {data.length === 0 ? (
          <div className="h-full flex items-center justify-center text-sm text-muted-foreground">Chưa có lịch sử 24 giờ từ board</div>
        ) : (
          <ResponsiveContainer width="100%" height="100%">
            <BarChart data={data} margin={{ top: 8, right: 8, left: -12, bottom: 0 }}>
              <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="currentColor" className="opacity-10" />
              <XAxis dataKey="h" tick={{ fontSize: 10, fill: "var(--color-muted-foreground)" }} interval="preserveStartEnd" minTickGap={14} axisLine={false} tickLine={false} />
              <YAxis tick={{ fontSize: 10, fill: "var(--color-muted-foreground)" }} axisLine={false} tickLine={false} width={48} unit="" />
              <Tooltip
                cursor={{ fill: "var(--color-muted)", opacity: 0.5 }}
                formatter={(v, name) => [`${fmt(Number(v), 1)} mWh`, name]}
                labelFormatter={(l) => `Giờ ${l}`}
                contentStyle={{ background: "var(--color-popover)", borderColor: "var(--color-border)", borderRadius: 10, fontSize: 12 }}
              />
              {sel === "all" && <Legend iconType="circle" iconSize={8} wrapperStyle={{ fontSize: 12 }} />}
              {ids.map((id) => (
                <Bar key={id} dataKey={`l${id}`} name={`Đèn ${id}`} stackId="e" fill={COLORS[id - 1]} radius={sel === "all" ? 0 : [4, 4, 0, 0]} isAnimationActive={false} />
              ))}
            </BarChart>
          </ResponsiveContainer>
        )}
      </div>
      <p className="text-xs text-muted-foreground">Đơn vị mWh mỗi giờ. Cột cuối là giờ hiện tại, đang cộng dồn.</p>
    </div>
  )
}
