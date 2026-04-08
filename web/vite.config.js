import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { viteSingleFile } from 'vite-plugin-singlefile' // 引入插件
// https://vite.dev/config/
export default defineConfig({
  plugins: [vue(), viteSingleFile()],
})
