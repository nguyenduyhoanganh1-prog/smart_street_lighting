import { useEffect, useRef, useState } from "react"
import { Bell, BellOff, CalendarDays, LogOut, Radio, Trash2, TriangleAlert, Unplug, Wifi, Zap } from "lucide-react"
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { AlertPanel } from "@/components/alert-panel"
import { ControlPanel } from "@/components/control-panel"
import { CurrentChart } from "@/components/current-chart"
import { Energy24h, EnergyTable } from "@/components/energy-panel"
import { HistoryLog } from "@/components/history-log"
import { LampCard } from "@/components/lamp-card"
import { LightPanel } from "@/components/light-panel"
import { LoginScreen } from "@/components/login"
import { Panel } from "@/components/panel"
import { SettingsPanel } from "@/components/settings-panel"
import { Street } from "@/components/street"
import { clock, formatRelativeTime, longDate } from "@/lib/format"
import { MODE_TEXT } from "@/lib/protocol"
import { useStation } from "@/lib/station"
import { ThemeToggle } from "@/lib/theme"

/** Tiếng bíp tạo bằng Web Audio, không cần tải file âm thanh từ Internet */
function beep(ctx: AudioContext) {
  const o = ctx.createOscillator()
  const g = ctx.createGain()
  o.type = "square"
  o.frequency.value = 880
  g.gain.value = 0.05
  o.connect(g).connect(ctx.destination)
  o.start()
  o.stop(ctx.currentTime + 0.3)
}

export default function App() {
  const st = useStation()
  const { state, lamps, config, alert } = st
  const [muted, setMuted] = useState(true)
  const audioRef = useRef<AudioContext | null>(null)
  const [now, setNow] = useState(Date.now())

  useEffect(() => {
    const t = setInterval(() => setNow(Date.now()), 1000)
    return () => clearInterval(t)
  }, [])

  const faulty = lamps?.filter((l) => l.fault > 0) ?? []
  const danger = faulty.length > 0 || !!alert

  useEffect(() => {
    const ctx = audioRef.current
    if (!ctx || !danger || muted) return
    beep(ctx)
    const t = setInterval(() => beep(ctx), 1500)
    return () => clearInterval(t)
  }, [danger, muted])

  useEffect(() => {
    document.title = danger ? `(${faulty.length + (alert ? 1 : 0)}) Cảnh báo - Đèn Công Cộng Nhóm 15` : "Đèn Công Cộng Nhóm 15"
  }, [danger, faulty.length, alert])

  const inDash = st.source === "sim" || (st.source === "mqtt" && (st.status === "connected" || st.lastAt !== null || st.config !== null))
  if (!inDash) return <LoginScreen onConnect={st.connect} onSim={st.startSim} status={st.status} error={st.error} />

  const linkOk = st.source === "sim" || st.status === "connected"
  const boardOk = st.source === "sim" || st.boardOnline === true
  const stale = !linkOk || !boardOk || !lamps
  const disabled = !linkOk || !boardOk
  const manual = state?.mode === "manual"

  return (
    <div className="min-h-screen bg-background text-foreground flex flex-col font-sans antialiased overflow-x-hidden">
      <header className="sticky top-0 z-50 w-full border-b border-frame/30 bg-background/80 backdrop-blur-md shadow-sm">
        <div className="container mx-auto px-4 h-16 flex items-center justify-between gap-3">
          <div className="flex items-center gap-3 min-w-0">
            <div className="flex h-10 w-10 shrink-0 items-center justify-center rounded-xl bg-gradient-to-br from-frame to-[#0a8f52] text-white shadow-lg shadow-frame/20">
              <Zap className="h-5 w-5" />
            </div>
            <div className="min-w-0">
              <h1 className="text-base sm:text-lg font-bold tracking-tight truncate">Đèn Công Cộng Nhóm 15</h1>
              <p className="text-xs font-medium text-muted-foreground flex items-center gap-1.5 truncate">
                {st.source === "sim" ? (
                  <span className="flex items-center gap-1.5 text-sodium"><Radio className="h-3.5 w-3.5" /> Dữ liệu giả lập</span>
                ) : !linkOk ? (
                  <span className="flex items-center gap-1.5 text-destructive animate-pulse"><Unplug className="h-3.5 w-3.5" /> Đang kết nối lại broker MQTT</span>
                ) : boardOk ? (
                  <span className="flex items-center gap-1.5 text-ok"><Wifi className="h-3.5 w-3.5" /> Board trực tuyến</span>
                ) : (
                  <span className="flex items-center gap-1.5 text-destructive"><Unplug className="h-3.5 w-3.5" /> Board ESP32 offline</span>
                )}
                <span className="hidden sm:inline">&bull; {st.lastAt ? formatRelativeTime(st.lastAt) : "Đang chờ dữ liệu..."}</span>
              </p>
            </div>
          </div>
          <div className="flex items-center gap-2 shrink-0">
            <div className="hidden md:flex flex-col items-end mr-2 leading-tight">
              <span className="text-xs font-medium text-muted-foreground">{longDate(now)}</span>
              <span className="text-base font-bold tnum">{clock(now)}</span>
            </div>
            <Button
              variant="outline"
              size="icon"
              className="h-9 w-9 rounded-full border-frame/20 hover:bg-frame/5"
              onClick={() => {
                if (muted) {
                  try {
                    audioRef.current ??= new AudioContext()
                    void audioRef.current.resume()
                  } catch {
                    /* trình duyệt không hỗ trợ âm thanh */
                  }
                }
                setMuted(!muted)
              }}
              title={muted ? "Bật âm cảnh báo" : "Tắt âm"}
              aria-label={muted ? "Bật âm cảnh báo" : "Tắt âm"}
              aria-pressed={!muted}
            >
              {muted ? <BellOff className="h-4 w-4" /> : <Bell className="h-4 w-4 text-frame" />}
            </Button>
            <ThemeToggle />
            <Button variant="outline" size="icon" className="h-9 w-9 rounded-full" onClick={st.disconnect} title="Thoát" aria-label="Thoát">
              <LogOut className="h-4 w-4" />
            </Button>
          </div>
        </div>
      </header>

      <main className="flex-1 container mx-auto p-4 sm:p-6 lg:p-8 space-y-8">
        <p className="md:hidden flex items-center gap-1.5 text-sm font-medium text-muted-foreground">
          <CalendarDays className="h-4 w-4" /> {longDate(now)} <span className="font-bold text-foreground tnum">{clock(now)}</span>
        </p>

        {faulty.length > 0 && (
          <Alert variant="destructive" className="border-2 shadow-lg shadow-destructive/20">
            <TriangleAlert className="h-5 w-5 animate-pulse" />
            <AlertTitle className="text-lg font-bold">Phát hiện {faulty.length} đèn hỏng</AlertTitle>
            <AlertDescription className="text-sm font-medium opacity-90">{faulty.map((l) => `Đèn ${l.id}: ${l.msg}`).join(". ")}</AlertDescription>
          </Alert>
        )}

        {alert && <AlertPanel alert={alert} image={st.image} since={st.alertSince} onAck={() => st.cmd.ack(alert.id)} />}

        <section className="rounded-2xl overflow-hidden border border-frame/30 shadow-2xl shadow-frame/5 bg-night-2/50 relative">
          <div className="absolute top-4 left-4 z-10 hidden sm:flex gap-2">
            <Badge variant="outline" className="bg-night/60 text-night-fg border-night-muted/30 backdrop-blur-md">
              <Radio className="mr-1.5 h-3 w-3 text-sodium animate-pulse" /> Chế độ {state?.mode ? (MODE_TEXT[state.mode] ?? state.mode).toLowerCase() : "–"}
            </Badge>
          </div>
          <Street lamps={lamps} state={state} stale={stale} />
        </section>

        <div className="grid grid-cols-1 lg:grid-cols-12 gap-8">
          <div className="lg:col-span-8 space-y-8">
            <Panel title="Điều khiển">
              <ControlPanel state={state} lamps={lamps} st={st} disabled={disabled} />
            </Panel>
            <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
              {[1, 2, 3, 4].map((id) => (
                <LampCard key={id} index={id} lamp={lamps?.find((l) => l.id === id)} stale={stale} manual={manual && !disabled} onDuty={(d) => st.cmd.setLamp(id, d)} />
              ))}
            </div>
          </div>
          <div className="lg:col-span-4 space-y-8">
            <Panel title="Cường độ ánh sáng">
              <LightPanel state={state} config={config} series={st.lux} />
            </Panel>
            <Panel title="Dòng điện từng bóng (mA)">
              <div className="p-1"><CurrentChart history={st.current} stale={stale} /></div>
            </Panel>
          </div>
        </div>

        <Panel title="Điện năng từng bóng">
          <div className="grid grid-cols-1 2xl:grid-cols-2 gap-6 p-5">
            <div className="overflow-x-auto"><EnergyTable lamps={lamps} /></div>
            <Energy24h history={st.history24} />
          </div>
        </Panel>

        <div className="grid grid-cols-1 lg:grid-cols-12 gap-8">
          <Panel title="Cài đặt" className="lg:col-span-7">
            <SettingsPanel config={config} state={state} st={st} disabled={disabled} />
          </Panel>
          <Panel
            title="Lịch sử"
            className="lg:col-span-5"
            action={
              st.log.length > 0 && (
                <Button variant="ghost" size="sm" className="text-muted-foreground" onClick={() => confirm("Xoá toàn bộ lịch sử trên trình duyệt này?") && st.clearLog()}>
                  <Trash2 className="h-4 w-4" /> Xoá
                </Button>
              )
            }
          >
            <div className="h-[460px] flex flex-col p-1">
              <HistoryLog logs={st.log} />
            </div>
          </Panel>
        </div>
      </main>

      <footer className="mt-8 border-t border-frame/20 bg-muted/30 py-6 text-center text-sm font-medium text-muted-foreground">
        <p>Đồ án "Đèn công cộng thông minh tiết kiệm năng lượng" &copy; 2026</p>
        <p className="opacity-70 mt-1">Nhóm 15 • Thiết kế hệ thống nhúng</p>
      </footer>
    </div>
  )
}
