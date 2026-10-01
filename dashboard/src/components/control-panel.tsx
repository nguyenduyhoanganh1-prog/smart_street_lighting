import { Car, Footprints, Gauge, Hand, MoonStar, TriangleAlert, Wrench } from "lucide-react"
import { Button } from "@/components/ui/button"
import { Switch } from "@/components/ui/switch"
import { MODE_TEXT, type Lamp, type StatePayload } from "@/lib/protocol"
import type { Station } from "@/lib/station"

function Row({ id, icon, title, desc, checked, disabled, onChange }: { id: string; icon: React.ReactNode; title: string; desc: string; checked: boolean; disabled?: boolean; onChange: (v: boolean) => void }) {
  return (
    <div className="flex items-center justify-between gap-4 py-3">
      <label htmlFor={id} className="flex items-start gap-3 min-w-0 cursor-pointer">
        <span className="mt-0.5 text-frame">{icon}</span>
        <span>
          <span className="block font-semibold">{title}</span>
          <span className="block text-sm text-muted-foreground">{desc}</span>
        </span>
      </label>
      <Switch id={id} checked={checked} disabled={disabled} onCheckedChange={onChange} className="data-[state=checked]:bg-frame" />
    </div>
  )
}

export function ControlPanel({ state, lamps, st, disabled }: { state: StatePayload | null; lamps: Lamp[] | null; st: Station; disabled: boolean }) {
  const mode = state?.mode
  const sim = st.source === "sim" ? st.sim.current : null
  const faulty = lamps?.filter((l) => l.fault) ?? []
  return (
    <div className="divide-y divide-border px-5 py-2">
      <Row
        id="manual"
        icon={<Hand className="h-5 w-5" />}
        title="Chỉnh tay độ sáng từng đèn"
        desc="Bật để kéo độ sáng ở thẻ từng đèn. Board tự quay về tự động sau 10 phút."
        checked={mode === "manual"}
        disabled={disabled}
        onChange={(v) => st.cmd.setMode(v ? "manual" : "auto")}
      />
      <Row
        id="force-night"
        icon={<MoonStar className="h-5 w-5" />}
        title="Ép chế độ đêm"
        desc="Dùng khi trình diễn mà không cần tắt đèn phòng."
        checked={!!state?.force_night}
        disabled={disabled}
        onChange={(v) => st.cmd.setForceNight(v)}
      />
      {mode === "schedule" && (
        <p className="flex items-start gap-2 py-3 text-sm font-medium text-destructive">
          <TriangleAlert className="mt-0.5 h-4 w-4 shrink-0" />
          Cảm biến ánh sáng hoặc camera lỗi, board đang chạy chế độ theo giờ ({state?.sensor_error ?? "xem Lịch sử"}).
        </p>
      )}
      <dl className="grid grid-cols-2 sm:grid-cols-4 gap-3 py-4 text-sm">
        <div>
          <dt className="text-muted-foreground">Chế độ</dt>
          <dd className="font-bold">{mode ? MODE_TEXT[mode] ?? mode : "–"}</dd>
        </div>
        <div>
          <dt className="flex items-center gap-1 text-muted-foreground"><Footprints className="h-3.5 w-3.5" /> Người</dt>
          <dd className="font-bold tnum">{state?.persons ?? "–"}</dd>
        </div>
        <div>
          <dt className="flex items-center gap-1 text-muted-foreground"><Car className="h-3.5 w-3.5" /> Xe</dt>
          <dd className="font-bold tnum">{state?.vehicles ?? "–"}</dd>
        </div>
        <div>
          <dt className="flex items-center gap-1 text-muted-foreground"><Gauge className="h-3.5 w-3.5" /> Nhận diện</dt>
          <dd className="font-bold tnum">{state?.fps ? `${state.fps} fps` : "Tạm dừng"}</dd>
        </div>
      </dl>
      {sim && (
        <div className="flex flex-wrap items-center gap-2 py-3">
          <span className="text-sm font-semibold text-muted-foreground mr-1">Trình diễn:</span>
          <Button
            size="sm"
            variant="outline"
            className="rounded-full"
            onClick={() => {
              if (faulty.length) faulty.forEach((l) => sim.repair(l.id))
              else sim.fault(1 + Math.floor(Math.random() * 4))
            }}
          >
            <Wrench className="h-3.5 w-3.5" /> {faulty.length ? "Sửa bóng hỏng" : "Cho 1 bóng hỏng"}
          </Button>
          <Button size="sm" variant="outline" className="rounded-full" disabled={!!st.alert} onClick={() => sim.stopNow()}>
            <Car className="h-3.5 w-3.5" /> Cho 1 xe dừng lâu
          </Button>
        </div>
      )}
    </div>
  )
}
