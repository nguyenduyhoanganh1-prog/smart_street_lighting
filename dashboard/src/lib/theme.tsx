import { useEffect, useState } from "react"
import { Moon, Sun } from "lucide-react"
import { Button } from "@/components/ui/button"

type Mode = "light" | "dark"
const KEY = "dencongcong-theme"

function systemMode(): Mode {
  const forced = document.documentElement.getAttribute("data-theme")
  if (forced === "light" || forced === "dark") return forced
  return window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light"
}
function savedMode(): Mode | null {
  try {
    const v = localStorage.getItem(KEY)
    return v === "light" || v === "dark" ? v : null
  } catch {
    return null
  }
}

/** Nút đổi giao diện sáng/tối. Mặc định theo máy; bấm thì nhớ lựa chọn trên trình duyệt này. */
export function ThemeToggle() {
  const [mode, setMode] = useState<Mode>(() => savedMode() ?? systemMode())

  useEffect(() => {
    document.documentElement.classList.toggle("dark", mode === "dark")
  }, [mode])

  // Chưa chọn tay thì đi theo cài đặt hệ thống
  useEffect(() => {
    const mq = window.matchMedia("(prefers-color-scheme: dark)")
    const onChange = () => !savedMode() && setMode(systemMode())
    mq.addEventListener("change", onChange)
    return () => mq.removeEventListener("change", onChange)
  }, [])

  const next: Mode = mode === "dark" ? "light" : "dark"
  return (
    <Button
      variant="outline"
      size="icon"
      className="h-9 w-9 rounded-full border-frame/20 hover:bg-frame/5"
      onClick={() => {
        try {
          localStorage.setItem(KEY, next)
        } catch {
          /* trình duyệt chặn lưu thì vẫn đổi được trong lần xem này */
        }
        setMode(next)
      }}
      title={next === "dark" ? "Chuyển sang giao diện tối" : "Chuyển sang giao diện sáng"}
      aria-label={next === "dark" ? "Chuyển sang giao diện tối" : "Chuyển sang giao diện sáng"}
    >
      {mode === "dark" ? <Sun className="h-4 w-4" /> : <Moon className="h-4 w-4" />}
    </Button>
  )
}
