import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { viteSingleFile } from 'vite-plugin-singlefile' // 引入插件
// https://vite.dev/config/
export default defineConfig({
  server: {
    host: '0.0.0.0',
    port: 5174,
    strictPort: true,
  },
  plugins: [vue(), viteSingleFile()],
})
