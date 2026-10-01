import { CheckCircle2, Info, TriangleAlert } from "lucide-react"
import { clock, dateStr } from "@/lib/format"
import type { LogEntry } from "@/lib/station"

/** Lịch sử sự kiện, có ngày tháng năm, nhóm theo ngày. Lưu trên trình duyệt này nên mở lại trang vẫn còn. */
export function HistoryLog({ logs }: { logs: LogEntry[] }) {
  if (logs.length === 0) return <div className="flex-1 flex items-center justify-center text-muted-foreground font-medium text-sm">Chưa có sự kiện nào</div>
  let lastDay = ""
  return (
    <div className="flex-1 w-full overflow-y-auto px-2">
      <ol className="space-y-2 p-3">
        {logs.map((log) => {
          const day = dateStr(log.at)
          const head = day !== lastDay
          lastDay = day
          return (
            <li key={log.id}>
              {head && <p className="sticky top-0 z-10 -mx-1 mb-2 mt-1 bg-card px-1 py-1 text-xs font-bold uppercase tracking-wider text-muted-foreground">Ngày {day}</p>}
              <div className={`flex items-start gap-3 rounded-xl border p-3 text-sm ${log.kind === "bad" ? "bg-destructive/10 border-destructive/20 text-destructive" : "bg-muted/40 border-border/50"}`}>
                <span className={`mt-0.5 shrink-0 ${log.kind === "bad" ? "" : log.kind === "ok" ? "text-ok" : "text-frame"}`}>
                  {log.kind === "bad" ? <TriangleAlert className="h-4 w-4" /> : log.kind === "ok" ? <CheckCircle2 className="h-4 w-4" /> : <Info className="h-4 w-4" />}
                </span>
                <div className="min-w-0 flex-1">
                  <p className="font-semibold">{log.text}</p>
                  <time dateTime={new Date(log.at).toISOString()} className="mt-0.5 block text-xs opacity-70 tnum">
                    {day} lúc {clock(log.at)}
                  </time>
                </div>
              </div>
            </li>
          )
        })}
      </ol>
    </div>
  )
}
