import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
  server: {
    host: true,       // Bind to 0.0.0.0 so LAN devices can connect (not just localhost)
    port: 5173,
    strictPort: true, // Fail fast if 5173 is taken — avoids silent port changes
    proxy: {
      // All browsers (local and remote) connect to ws://<host>:5173/ws.
      // Vite proxies the WebSocket upgrade to the bridge on localhost:8081.
      // This means only port 5173 needs to be open in the firewall — not 8081.
      '/ws': {
        target: 'ws://localhost:8081',
        ws: true,
        changeOrigin: true,
        rewrite: (path) => path.replace(/^\/ws/, ''),
      },
    },
  },
})
