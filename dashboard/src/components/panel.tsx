import type { ReactNode } from "react"
import { cn } from "@/lib/utils"

/** Khung chung cho các mục: tiêu đề có vạch xanh, nội dung trong thẻ viền xanh */
export function Panel({ title, icon, action, className, children }: { title: string; icon?: ReactNode; action?: ReactNode; className?: string; children: ReactNode }) {
  return (
    <section className={cn("space-y-4", className)}>
      <div className="flex items-center justify-between gap-3">
        <h2 className="text-xl font-bold tracking-tight flex items-center gap-2">
          <span className="h-6 w-1.5 rounded-full bg-frame" />
          {icon}
          {title}
        </h2>
        {action}
      </div>
      <div className="bg-card rounded-2xl border border-frame/30 shadow-xl shadow-frame/5">{children}</div>
    </section>
  )
}
