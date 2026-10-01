import path from "path"
import tailwindcss from "@tailwindcss/vite"
import react from "@vitejs/plugin-react"
import { defineConfig } from "vite"
import { viteSingleFile } from "vite-plugin-singlefile"

// Build ra 1 file HTML (đã gồm font, CSS, JS) vào ../docs/index.html để đăng lên GitHub Pages (Settings → Pages → thư mục /docs)
export default defineConfig({
  base: "./",
  plugins: [react(), tailwindcss(), viteSingleFile()],
  resolve: { alias: { "@": path.resolve(import.meta.dirname, "./src") } },
  build: { outDir: "../docs", emptyOutDir: true, assetsInlineLimit: 100_000_000, cssCodeSplit: false },
})
