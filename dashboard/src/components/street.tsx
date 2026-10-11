import { AlertTriangle, Moon, Stars, Sun } from "lucide-react"
import { cn } from "@/lib/utils"
import { FAULT_SHORT, type Lamp, type StatePayload } from "@/lib/protocol"
import { fmt } from "@/lib/format"

const POSTS = [150, 350, 550, 750] // Căn đều 4 cột cho đẹp hơn
const GROUND = 220 // Hạ thấp mặt đất một chút

export function Street({ lamps: ls, state, stale }: { lamps: Lamp[] | null; state: StatePayload | null; stale: boolean }) {
  const lamps = ls ?? []
  const totalP = lamps.reduce((a, l) => a + l.p, 0)
  const zones = state?.zones ?? []
  const activeZones = [
    ...new Set(
      zones.filter(
        (zone) =>
          Number.isInteger(zone) &&
          zone >= 1 &&
          zone <= 4,
      ),
    ),
  ]
  const onCount = lamps.filter((l) => l.duty > 0 && !l.fault).length
  const night = state?.night

  return (
    <div className={cn("relative w-full aspect-[16/10] sm:aspect-[21/9] md:aspect-[3/1] bg-gradient-to-b transition-colors duration-1000 flex", night === false ? "from-[#3b6a9a] to-[#1d3553]" : "from-night to-night-2", "items-center justify-center overflow-hidden")}>
      {/* Background nhấp nháy sao/mây */}
      <div className="absolute inset-0 opacity-30 pointer-events-none">
        <div className="absolute top-4 left-10 text-white/40"><Stars size={24} /></div>
        <div className="absolute top-12 left-1/4 text-white/20"><Stars size={16} /></div>
        <div className="absolute top-8 right-1/3 text-white/50">{night === false ? <Sun size={36} className="text-sodium" /> : <Moon size={32} />}</div>
      </div>

      <svg
        viewBox="0 0 900 250"
        preserveAspectRatio="xMidYMax meet"
        className={cn(
          "w-full h-full text-night-fg drop-shadow-md",
          stale && "opacity-50 grayscale transition-all duration-1000"
        )}
      >
        {/* Đường phố (Ground) */}
        <rect x="0" y={GROUND} width="900" height="30" fill="#111827" />
        <rect x="0" y={GROUND} width="900" height="2" fill="var(--color-pole-head)" />
        <path d="M50 235 h40 M150 235 h40 M250 235 h40 M350 235 h40 M450 235 h40 M550 235 h40 M650 235 h40 M750 235 h40 M850 235 h40" stroke="#374151" strokeWidth="2" strokeDasharray="20 20" />

        {POSTS.map((x, i) => {
          const l = lamps.find((x) => x.id === i + 1)
          const isOk = !!l && l.duty > 0 && !l.fault
          const glow = isOk ? 0.35 + 0.65 * (l.duty / 100) : 0 // đèn nền 20% thì chùm sáng mờ hơn
          const isFault = (l?.fault ?? 0) > 0

          return (
            <g key={i} className="transition-all duration-500">
              {/* Thân cột */}
              <rect x={x - 4} y="50" width="8" height={GROUND - 50} fill="url(#poleGrad)" rx="2" />
              {/* Cần đèn uốn cong */}
              <path
                d={`M${x},55 Q${x + 20},20 ${x + 50},20`}
                fill="none"
                stroke="var(--color-pole-head)"
                strokeWidth="6"
                strokeLinecap="round"
              />
              {/* Chóa đèn */}
              <path
                d={`M${x + 40},17 L${x + 60},17 L${x + 65},25 L${x + 35},25 Z`}
                fill="#2d3748"
              />

              {/* Ánh sáng mô phỏng chiếu xuống đường */}
              {isOk && (
                <polygon
                  points={`${x + 50},25 ${x - 50},${GROUND} ${x + 150},${GROUND}`}
                  fill="url(#lightBeam)"
                  className="animate-in fade-in duration-1000 transition-opacity"
                  style={{ mixBlendMode: 'screen', opacity: glow }}
                />
              )}

              {/* Bóng LED */}
              <circle
                cx={x + 50}
                cy="25"
                r="4"
                fill={isFault ? "var(--color-destructive)" : isOk ? "var(--color-sodium)" : "#4b5563"}
                className={isFault ? "animate-ping" : ""}
              />
              {/* Vầng sáng (Glow) */}
              {isOk && (
                <circle
                  cx={x + 50}
                  cy="25"
                  r="16"
                  fill="var(--color-sodium)"
                  className="animate-pulse"
                  style={{ filter: "blur(6px)", opacity: 0.4 * glow }}
                />
              )}

              {/* Thông tin gắn trên thân cột (chỉ hiện khi có kết nối) */}
              {l && (
                <g transform={`translate(${x}, ${GROUND - 80})`} className="opacity-90">
                  <rect x="-48" y="0" width="96" height="42" fill="var(--color-night)" rx="6" stroke="var(--color-night-muted)" strokeWidth="1" />
                  <text x="0" y="16" textAnchor="middle" fontSize="11" fontWeight="bold" fill="white" className="tnum font-sans">
                    Vùng {i + 1}
                  </text>
                  <text x="0" y="32" textAnchor="middle" fontSize="12" fill={isFault ? "var(--color-destructive)" : "var(--color-night-fg)"} className="tnum font-sans font-bold">
                    {isFault ? FAULT_SHORT[l.fault] : `${Math.round(l.duty)}% · ${fmt(l.p, 0)}mW`}
                  </text>
                  {isFault && (
                    <g transform="translate(-8, -20)">
                      <circle cx="8" cy="8" r="10" fill="var(--color-destructive)" />
                      <text x="8" y="12" textAnchor="middle" fontSize="10" fill="white" fontWeight="bold">!</text>
                    </g>
                  )}
                </g>
              )}
            </g>
          )
        })}

        {/* Vùng đang có người/xe (camera + AI phát hiện) */}
        {activeZones.map((z) => (
          <g key={`z${z}`} transform={`translate(${POSTS[z - 1] + 50}, ${GROUND - 14})`}>
            <rect x="-34" y="-9" width="68" height="18" rx="9" fill="var(--color-sodium)" opacity="0.9" />
            <text x="0" y="4" textAnchor="middle" fontSize="10" fontWeight="bold" fill="#1d1203" className="font-sans">CÓ NGƯỜI/XE</text>
          </g>
        ))}

        {/* Định nghĩa Gradients */}
        <defs>
          <linearGradient id="poleGrad" x1="0" y1="0" x2="1" y2="0">
            <stop offset="0%" stopColor="#475569" />
            <stop offset="50%" stopColor="#94a3b8" />
            <stop offset="100%" stopColor="#334155" />
          </linearGradient>
          <linearGradient id="lightBeam" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor="var(--color-sodium)" stopOpacity="0.35" />
            <stop offset="100%" stopColor="var(--color-sodium)" stopOpacity="0.0" />
          </linearGradient>
        </defs>
      </svg>

      {/* Thông số tổng overlay */}
      <div className="absolute top-3 left-3 sm:top-auto sm:bottom-4 sm:left-4 bg-night/80 backdrop-blur-md px-3 sm:px-4 py-2 sm:py-2.5 rounded-xl border border-white/10 text-night-fg flex gap-3 sm:gap-6 shadow-2xl">
        <div className="flex flex-col">
          <span className="text-[10px] uppercase font-bold text-night-muted tracking-wider">{night === false ? "Ban ngày" : night ? "Ban đêm" : "Ánh sáng"}</span>
          <span className="text-xl font-black tnum leading-tight">{state?.lux == null ? "–" : `${fmt(state.lux, 0)} lux`}</span>
        </div>
        <div className="w-px bg-white/10" />
        <div className="flex flex-col">
          <span className="text-[10px] uppercase font-bold text-night-muted tracking-wider">Đang sáng</span>
          <span className="text-xl font-black tnum leading-tight">{onCount}/4</span>
        </div>
        <div className="w-px bg-white/10" />
        <div className="flex flex-col">
          <span className="text-[10px] uppercase font-bold text-night-muted tracking-wider">Tổng công suất</span>
          <span className="text-xl font-black tnum leading-tight">{!ls ? "–" : totalP >= 1000 ? `${fmt(totalP / 1000, 2)} W` : `${fmt(totalP, 0)} mW`}</span>
        </div>
      </div>

      {stale && (
        <div className="absolute inset-0 bg-night-2/60 backdrop-blur-sm flex items-center justify-center">
          <div className="flex items-center gap-2 bg-destructive/90 text-white px-5 py-2.5 rounded-full font-bold shadow-lg shadow-destructive/20 animate-pulse">
            <AlertTriangle className="h-5 w-5" />
            ĐANG CHỜ DỮ LIỆU TỪ BOARD...
          </div>
        </div>
      )}
    </div>
  )
}
