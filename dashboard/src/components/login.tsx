import { useState } from "react"
import { Eye, EyeOff, LoaderCircle, Lock, PlayCircle, Zap } from "lucide-react"
import { Button } from "@/components/ui/button"
import { Card, CardContent, CardDescription, CardFooter, CardHeader, CardTitle } from "@/components/ui/card"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { ThemeToggle } from "@/lib/theme"
import type { ConnStatus, Login } from "@/lib/station"

const SAVE_KEY = "dencongcong-mqtt"
// Broker EMQX Cloud Serverless của nhóm (địa chỉ không phải bí mật; tài khoản/mật khẩu thì người dùng tự nhập)
const DEFAULT_HOST = "yb37c111.ala.asia-southeast1.emqxsl.com"
const DEFAULT_PORT = 8084

function saved(): { host: string; port: number; username: string } {
  try {
    const v = JSON.parse(localStorage.getItem(SAVE_KEY) ?? "{}")
    return { host: v.host || DEFAULT_HOST, port: v.port || DEFAULT_PORT, username: v.username ?? "" }
  } catch {
    return { host: DEFAULT_HOST, port: DEFAULT_PORT, username: "" }
  }
}

/** Màn hình nhập tài khoản MQTT. Mật khẩu không lưu ở đâu cả, chỉ nằm trong bộ nhớ trang. */
export function LoginScreen({ onConnect, onSim, status, error }: { onConnect: (l: Login) => void; onSim: () => void; status: ConnStatus; error: string | null }) {
  const [form, setForm] = useState(() => ({ ...saved(), password: "" }))
  const [show, setShow] = useState(false)
  const busy = status === "connecting"

  return (
    <div className="min-h-screen bg-background flex flex-col">
      <div className="flex justify-end p-4">
        <ThemeToggle />
      </div>
      <main className="flex-1 flex items-start sm:items-center justify-center px-4 pb-10">
        <Card className="w-full max-w-md shadow-xl">
          <CardHeader>
            <div className="mb-2 flex h-11 w-11 items-center justify-center rounded-xl bg-frame text-white shadow-lg shadow-frame/20">
              <Zap className="h-5 w-5" />
            </div>
            <CardTitle className="text-xl">Đèn Công Cộng Nhóm 15</CardTitle>
            <CardDescription>Nhập tài khoản MQTT dành cho dashboard (EMQX Cloud) để xem và điều khiển 4 đèn.</CardDescription>
          </CardHeader>
          <form
            className="flex flex-col gap-6"
            onSubmit={(e) => {
              e.preventDefault()
              try {
                localStorage.setItem(SAVE_KEY, JSON.stringify({ host: form.host, port: form.port, username: form.username }))
              } catch {
                /* không lưu được thì thôi */
              }
              const host = form.host.trim()
              const username = form.username.trim()

              if (
                !host ||
                !username ||
                !Number.isInteger(form.port) ||
                form.port < 1 ||
                form.port > 65535
              ) {
                return
              }

              onConnect({
                ...form,
                host,
                username,
              })
            }}
          >
            <CardContent className="space-y-4">
              <div className="space-y-1.5">
                <Label htmlFor="host">Địa chỉ broker</Label>
                <Input id="host" required autoComplete="url" placeholder="xxxxxxxx.ala.asia-southeast1.emqxsl.com" value={form.host} onChange={(e) => setForm({ ...form, host: e.target.value })} />
              </div>
              <div className="grid grid-cols-[1fr_6rem] gap-3">
                <div className="space-y-1.5">
                  <Label htmlFor="user">Tên đăng nhập</Label>
                  <Input id="user" required autoComplete="username" value={form.username} onChange={(e) => setForm({ ...form, username: e.target.value })} />
                </div>
                <div className="space-y-1.5">
                  <Label htmlFor="port">Cổng</Label>
                  <Input
                    id="port"
                    type="number"
                    inputMode="numeric"
                    min={1}
                    max={65535}
                    required
                    value={form.port}
                    onChange={(e) =>
                      setForm({
                        ...form,
                        port: Number(e.target.value),
                      })
                    }
                  />
                </div>
              </div>
              <div className="space-y-1.5">
                <Label htmlFor="pass">Mật khẩu</Label>
                <div className="relative">
                  <Input id="pass" required type={show ? "text" : "password"} autoComplete="current-password" className="pr-10" value={form.password} onChange={(e) => setForm({ ...form, password: e.target.value })} />
                  <button type="button" onClick={() => setShow(!show)} className="absolute inset-y-0 right-0 flex w-10 items-center justify-center text-muted-foreground hover:text-foreground" aria-label={show ? "Ẩn mật khẩu" : "Hiện mật khẩu"}>
                    {show ? <EyeOff className="h-4 w-4" /> : <Eye className="h-4 w-4" />}
                  </button>
                </div>
              </div>
              {error && <p role="alert" className="rounded-lg bg-bad-soft px-3 py-2 text-sm font-medium text-destructive">{error}</p>}
              <p className="flex items-start gap-2 text-xs text-muted-foreground">
                <Lock className="mt-0.5 h-3.5 w-3.5 shrink-0" />
                Mặc định sử dụng WebSocket/TLS với EMQX Cloud
                (cổng 8084). Mật khẩu chỉ được giữ trong bộ nhớ
                của trang trong phiên hiện tại. Mật khẩu không lưu trong code hay trên máy, chỉ dùng cho lần mở trang này.
              </p>
            </CardContent>
            <CardFooter className="flex flex-col gap-3 pt-2">
              <Button type="submit" className="w-full bg-frame text-white hover:bg-frame/90" disabled={busy}>
                {busy && <LoaderCircle className="h-4 w-4 animate-spin" />}
                {busy ? "Đang kết nối..." : "Kết nối"}
              </Button>
              <Button type="button" variant="outline" className="w-full" onClick={onSim}>
                <PlayCircle className="h-4 w-4" /> Xem bằng dữ liệu giả lập
              </Button>
            </CardFooter>
          </form>
        </Card>
      </main>
    </div>
  )
}
