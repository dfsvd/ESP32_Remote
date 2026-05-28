<template>
  <div class="flex flex-col gap-5">
    <!-- ===== 顶部: 控制器模式横条 ===== -->
    <nav class="flex gap-1.5 sm:gap-2 flex-wrap items-center">
      <template v-for="(m, idx) in CONTROLLER_MODES" :key="m.id">
        <button type="button"
          @click="selectMode(m.id)"
          :class="['px-2.5 sm:px-4 py-2 sm:py-2.5 text-xs sm:text-sm font-bold rounded-xl transition-all whitespace-nowrap flex items-center gap-1.5 sm:gap-2',
            selected === m.id
              ? 'bg-darwin-amber/20 text-darwin-amber border border-darwin-amber/30'
              : 'text-darwin-muted border border-transparent hover:border-[var(--theme-border)]']"
        >
          <span class="inline-block w-2 h-2 sm:w-2.5 sm:h-2.5 rounded-full shrink-0" :style="{ backgroundColor: modeColor(m.id) }"></span>
          {{ lang === 'zh' ? m.zh : m.en }}
        </button>
      </template>
    </nav>

    <!-- ===== 主体: 左面板(灯效) + 右面板(颜色) ===== -->
    <div class="grid grid-cols-1 lg:grid-cols-[180px_minmax(0,1fr)] gap-5">
      <!-- 左: 模式 (LED 灯效类型) -->
      <div class="lg:sticky lg:top-20 lg:self-start">
        <div class="p-3 sm:p-4 rounded-2xl border border-[var(--theme-border-light)] bg-[var(--theme-panel-alt)] flex flex-row lg:flex-col gap-1.5 sm:gap-1 items-center lg:items-stretch">
          <span class="text-[0.65rem] sm:text-[0.7rem] font-bold uppercase tracking-widest text-darwin-muted lg:mb-2 shrink-0">{{ lang === 'zh' ? '模式' : 'Mode' }}</span>
          <button
            v-for="e in EFFECTS" :key="e.id" type="button"
            @click="setEffect(e.id)"
            :class="['px-3 sm:px-4 py-2 sm:py-3 text-xs sm:text-sm font-bold text-left rounded-xl transition-all flex items-center gap-2 sm:gap-3',
              cfg.effect === e.id
                ? 'bg-darwin-amber/20 text-darwin-amber'
                : 'text-darwin-muted hover:bg-[var(--theme-bg-hover)]']"
          >
            <span class="w-4 h-4 sm:w-5 sm:h-5 shrink-0" :class="e.iconClass"></span>
            <span class="lg:inline">{{ lang === 'zh' ? e.zh : e.en }}</span>
          </button>
        </div>
      </div>

      <!-- 右: 设置区 -->
      <div v-if="cfg" class="flex flex-col gap-5">
        <div class="p-5 rounded-2xl border border-[var(--theme-border-light)] bg-darwin-panel shadow-[0_14px_28px_var(--theme-shadow-sm)]">
          <span class="text-[0.7rem] font-bold uppercase tracking-widest text-darwin-muted block mb-4">{{ lang === 'zh' ? '设置灯光' : 'Light Settings' }}</span>
          <div class="flex flex-col sm:flex-row sm:flex-wrap items-start gap-4 sm:gap-8 lg:gap-12">
            <!-- 列1: 控件 -->
            <div class="flex flex-col gap-4 sm:gap-6 sm:w-[180px] w-full">
              <!-- 速度 (闪烁/炫彩时可调) -->
              <div class="flex flex-col gap-2 sm:gap-3 flex-1 sm:flex-none"
                :class="cfg.effect === 0 ? 'opacity-40' : ''">
                <div class="flex justify-between text-xs">
                  <span class="text-darwin-muted">{{ lang === 'zh' ? '速度' : 'Speed' }}</span>
                  <span class="text-darwin-muted">{{ speedLabel }}</span>
                </div>
                <div ref="speedSlider"
                  class="relative h-6 touch-none"
                  :class="cfg.effect !== 0 ? 'cursor-pointer' : ''"
                  @mousedown="startSpeedDrag"
                  @touchstart.prevent="startSpeedDrag">
                  <div class="absolute top-1/2 -translate-y-1/2 left-0 right-0 h-1 bg-[var(--theme-bg-hover)] rounded-full pointer-events-none"></div>
                  <div class="absolute top-1/2 -translate-y-1/2 left-0 h-1 rounded-full bg-darwin-amber z-[1] pointer-events-none"
                    :style="{ width: (speedIdx - 1) * 25 + '%' }"></div>
                  <div v-for="i in 5" :key="i"
                    class="absolute top-1/2 w-1 h-1 rounded-full z-[3] pointer-events-none"
                    :class="i <= speedIdx ? 'bg-darwin-amber' : 'bg-[var(--theme-bg-active)]'"
                    :style="{ left: (i - 1) * 25 + '%', transform: 'translate(-50%, -50%)' }"></div>
                  <div class="absolute top-1/2 w-4 h-4 rounded-full bg-white shadow-[0_0_8px_var(--theme-knob-shadow)] z-[4] pointer-events-none"
                    :style="{ left: (speedIdx - 1) * 25 + '%', transform: 'translate(-50%, -50%)' }"></div>
                </div>
              </div>

              <!-- 亮度 -->
              <div class="flex flex-col gap-3">
                <div class="flex justify-between text-xs">
                  <span class="text-darwin-muted font-bold">{{ lang === 'zh' ? '亮度' : 'Brightness' }}</span>
                  <span class="text-darwin-muted">{{ cfg.brightness + 1 }}{{ lang === 'zh' ? '档' : '' }}</span>
                </div>
                <div ref="briSlider"
                  class="relative h-6 touch-none cursor-pointer"
                  @mousedown="startBriDrag"
                  @touchstart.prevent="startBriDrag">
                  <div class="absolute top-1/2 -translate-y-1/2 left-0 right-0 h-1 bg-[var(--theme-bg-hover)] rounded-full pointer-events-none"></div>
                  <div class="absolute top-1/2 -translate-y-1/2 left-0 h-1 rounded-full bg-darwin-amber z-[1] pointer-events-none"
                    :style="{ width: cfg.brightness * 25 + '%' }"></div>
                  <div v-for="i in 5" :key="i"
                    class="absolute top-1/2 w-1 h-1 rounded-full z-[3] pointer-events-none"
                    :class="i <= cfg.brightness + 1 ? 'bg-darwin-amber' : 'bg-[var(--theme-bg-active)]'"
                    :style="{ left: (i - 1) * 25 + '%', transform: 'translate(-50%, -50%)' }"></div>
                  <div class="absolute top-1/2 w-4 h-4 rounded-full bg-white shadow-[0_0_8px_var(--theme-knob-shadow)] z-[4] pointer-events-none"
                    :style="{ left: cfg.brightness * 25 + '%', transform: 'translate(-50%, -50%)' }"></div>
                </div>
              </div>

            </div>

            <!-- 列2: 取色器 -->
            <div class="flex flex-col gap-2.5 sm:gap-3.5 w-full sm:w-[220px]">
              <div class="flex justify-between items-center text-xs">
                <span class="text-darwin-muted font-bold">{{ lang === 'zh' ? '颜色' : 'Color' }}</span>
                <div class="w-6 h-6 sm:w-7 sm:h-7 rounded-full border-2 border-white shadow" :style="{ backgroundColor: curRgb }"></div>
              </div>
              <div class="w-full h-[120px] sm:h-[150px] rounded relative cursor-crosshair shadow-[inset_0_0_0_1px_rgba(0,0,0,0.15)] touch-none"
                :style="{ background: `linear-gradient(to top, #000, transparent), linear-gradient(to right, #fff, hsl(${hue}, 100%, 50%))` }"
                ref="satBox"
                @mousedown="startSatDrag"
                @touchstart.prevent="startSatDrag">
                <div class="absolute w-3.5 h-3.5 border-2 border-white rounded-full shadow -translate-x-1/2 -translate-y-1/2" :style="satDotStyle"></div>
              </div>
              <div class="w-full h-3.5 rounded-full mt-1 touch-none cursor-pointer"
                style="background: linear-gradient(to right, #f00, #ff0, #0f0, #0ff, #00f, #f0f, #f00)"
                ref="hueBar"
                @mousedown="startHueDrag"
                @touchstart.prevent="startHueDrag">
                <div class="absolute -top-[3px] w-5 h-5 rounded-full border-2 border-white shadow bg-transparent" :style="{ left: huePct + '%', transform: 'translateX(-50%)' }"></div>
              </div>
            </div>

            <!-- 列3: 数值 + 预设色板 -->
            <div class="flex flex-col sm:flex-row items-start gap-4 sm:gap-8 w-full sm:w-auto">
              <!-- RGB + Hex 输入 -->
              <div class="flex flex-row sm:flex-col gap-2 sm:gap-3 items-center sm:items-start">
                <div class="flex sm:flex-col gap-2 sm:gap-3">
                  <div class="flex items-center gap-1.5 sm:gap-2">
                    <span class="w-3 text-xs text-darwin-muted text-right">R</span>
                    <input class="w-12 sm:w-14 p-1 text-xs text-center bg-[var(--theme-input-bg)] border border-[var(--theme-input-border)] rounded text-[var(--theme-input-text)] outline-none [appearance:textfield] [&::-webkit-inner-spin-button]:appearance-none [&::-webkit-outer-spin-button]:appearance-none"
                      type="number" min="0" max="255" v-model.number="cfg.r" @input="onRgbInput">
                  </div>
                  <div class="flex items-center gap-1.5 sm:gap-2">
                    <span class="w-3 text-xs text-darwin-muted text-right">G</span>
                    <input class="w-12 sm:w-14 p-1 text-xs text-center bg-[var(--theme-input-bg)] border border-[var(--theme-input-border)] rounded text-[var(--theme-input-text)] outline-none [appearance:textfield] [&::-webkit-inner-spin-button]:appearance-none [&::-webkit-outer-spin-button]:appearance-none"
                      type="number" min="0" max="255" v-model.number="cfg.g" @input="onRgbInput">
                  </div>
                  <div class="flex items-center gap-1.5 sm:gap-2">
                    <span class="w-3 text-xs text-darwin-muted text-right">B</span>
                    <input class="w-12 sm:w-14 p-1 text-xs text-center bg-[var(--theme-input-bg)] border border-[var(--theme-input-border)] rounded text-[var(--theme-input-text)] outline-none [appearance:textfield] [&::-webkit-inner-spin-button]:appearance-none [&::-webkit-outer-spin-button]:appearance-none"
                      type="number" min="0" max="255" v-model.number="cfg.b" @input="onRgbInput">
                  </div>
                </div>
                <div class="flex items-center gap-1.5 sm:gap-2 sm:mt-1">
                  <span class="w-3 text-xs text-darwin-muted text-right">#</span>
                  <input class="w-14 sm:w-14 p-1 text-xs text-center bg-[var(--theme-input-bg)] border border-[var(--theme-input-border)] rounded text-[var(--theme-input-text)] outline-none"
                    v-model="hexDisplay" maxlength="6" @change="onHexInput" @keyup.enter="onHexInput">
                </div>
              </div>

              <!-- 预设色板: 移动端 8x2, 桌面 4x4 -->
              <div class="grid grid-cols-8 sm:grid-cols-4 gap-2.5 sm:gap-4 w-full sm:w-[192px]">
                <div v-for="c in PRESETS" :key="c"
                  class="aspect-square rounded-full cursor-pointer border transition-transform hover:scale-110"
                  :class="isPresetSelected(c) ? '!border-white shadow-[0_0_0_2px_var(--theme-panel),0_0_0_3px_#fff]' : 'border-[rgba(255,255,255,0.1)]'"
                  :style="{ backgroundColor: c }"
                  @click="pickPreset(c)"></div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, computed, watch, onMounted, onUnmounted } from 'vue'

// ===== 常量 =====
const CONTROLLER_MODES = [
  { id: 1, zh: 'CRSF',     en: 'CRSF' },
  { id: 2, zh: 'BLE',      en: 'BLE' },
  { id: 3, zh: 'USB HID',  en: 'USB HID' },
  { id: 4, zh: 'USB Xbox', en: 'USB Xbox' },
  { id: 5, zh: 'WiFi',     en: 'WiFi' },
  { id: 6, zh: '自动对频', en: 'Bind' },
]

const EFFECTS = [
  { id: 0, zh: '常亮',   en: 'Solid',     iconClass: 'icon-solid' },
  { id: 2, zh: '呼吸',   en: 'Breathing', iconClass: 'icon-breath' },
  { id: 1, zh: '闪烁',   en: 'Blink',     iconClass: 'icon-blink' },
  { id: 3, zh: '炫彩',   en: 'Rainbow',   iconClass: 'icon-rainbow' },
]

const PRESETS = [
  '#ffffff','#ff4d4d','#ff9900','#ffff00',
  '#00ff00','#ff00ff','#ff1493','#4169e1',
  '#8a2be2','#00bfff','#7fffd4','#5f9ea0',
  '#ffa500','#00008b','#000000','#4b0082',
]

const DEFAULTS = {
  1: { r:255, g:0,   b:0,   effect:0, brightness:4, interval:500 },
  2: { r:0,   g:0,   b:255, effect:0, brightness:4, interval:500 },
  3: { r:0,   g:255, b:255, effect:0, brightness:4, interval:500 },
  4: { r:0,   g:255, b:0,   effect:0, brightness:4, interval:500 },
  5: { r:0,   g:255, b:0,   effect:0, brightness:4, interval:500 },
  6: { r:255, g:255, b:0,   effect:1, brightness:4, interval:500 },
}

// ===== Props & Emits =====
const props = defineProps({
  lang: { type: String, default: 'zh' },
  ledConfig: { type: Array, default: () => [] },
  connected: { type: Boolean, default: false }
})
const emit = defineEmits(['setLedColor', 'requestLedConfig', 'saveLedConfig'])

// ===== 状态 =====
const selected = ref(1)
const hue = ref(0), sat = ref(100), bri = ref(100)
const cfg = reactive({ r: 255, g: 0, b: 0, effect: 0, brightness: 4, interval: 500 })
const drafts = reactive({})
for (let i = 1; i <= 6; i++) drafts[i] = { ...DEFAULTS[i] }

watch(() => props.ledConfig, (arr) => {
  if (!arr || arr.length < 7) return
  for (let i = 1; i <= 6; i++) {
    const m = arr[i]
    if (m) drafts[i] = { r: m.r, g: m.g, b: m.b, effect: m.e, brightness: m.brightness ?? 4, interval: m.interval ?? 500 }
  }
  loadMode(selected.value)
}, { deep: true })

const curRgb = computed(() => `rgb(${cfg.r},${cfg.g},${cfg.b})`)
const hexDisplay = computed(() => rgbToHex(cfg.r, cfg.g, cfg.b))
const huePct = computed(() => Math.round(hue.value / 360 * 100))
const satDotStyle = computed(() => ({ left: sat.value + '%', top: (100 - bri.value) + '%' }))

// ===== 模式/效果切换 =====
function selectMode(id) { saveDraft(); loadMode(id) }
function setEffect(e) {
  cfg.effect = e
  const steps = e === 2 ? BREATH_SPEED_STEPS : SPEED_STEPS
  if (steps.indexOf(cfg.interval) === -1) cfg.interval = steps[2]
  saveDraft(); emitChange()
}

function saveDraft() {
  const m = selected.value
  if (m >= 1 && m <= 6) drafts[m] = { r: cfg.r, g: cfg.g, b: cfg.b, effect: cfg.effect, brightness: cfg.brightness, interval: cfg.interval }
}

function loadMode(id) {
  selected.value = id
  const d = drafts[id] || DEFAULTS[id]
  cfg.r = d.r; cfg.g = d.g; cfg.b = d.b; cfg.effect = d.effect; cfg.brightness = d.brightness; cfg.interval = d.interval ?? 500
  const hsv = rgbToHsv(d.r, d.g, d.b)
  hue.value = hsv.h; sat.value = hsv.s; bri.value = hsv.v
}

const SPEED_STEPS = [100, 200, 500, 1000, 2000]
const BREATH_SPEED_STEPS = [3000, 4000, 5000, 6000, 7000]
const currentSpeedSteps = computed(() => {
  return cfg.effect === 2 ? BREATH_SPEED_STEPS : SPEED_STEPS
})
const speedIdx = computed(() => {
  const steps = currentSpeedSteps.value
  const idx = steps.indexOf(cfg.interval)
  return idx >= 0 ? idx + 1 : 3
})
const speedLabel = computed(() => {
  return cfg.interval >= 1000 ? `${cfg.interval / 1000}s` : `${cfg.interval}ms`
})

function calcSpeedFromX(clientX) {
  if (!speedSlider.value) return
  const rect = speedSlider.value.getBoundingClientRect()
  const pct = Math.max(0, Math.min(1, (clientX - rect.left) / rect.width))
  const steps = currentSpeedSteps.value
  cfg.interval = steps[Math.round(pct * 4)] ?? steps[2]
  saveDraft()
}
function calcBriFromX(clientX) {
  if (!briSlider.value) return
  const rect = briSlider.value.getBoundingClientRect()
  const pct = Math.max(0, Math.min(1, (clientX - rect.left) / rect.width))
  cfg.brightness = Math.round(pct * 4)
  saveDraft()
}
function modeColor(id) {
  const d = drafts[id] || DEFAULTS[id]
  return `rgb(${d.r},${d.g},${d.b})`
}

// ===== 取色器 =====
const satBox = ref(null), hueBar = ref(null), speedSlider = ref(null), briSlider = ref(null)
let dragging = null

function updatePos(e, type) {
  const el = type === 'hue' ? hueBar.value : satBox.value
  if (!el) return
  const rect = el.getBoundingClientRect()
  const x = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width))
  const y = Math.max(0, Math.min(1, (e.clientY - rect.top) / rect.height))
  if (type === 'hue') hue.value = Math.round(x * 360)
  else { sat.value = Math.round(x * 100); bri.value = Math.round((1 - y) * 100) }
  syncFromHsv()
}

function onMouseMove(e) {
  if (!dragging) return
  if (dragging === 'speed') { calcSpeedFromX(e.clientX); return }
  if (dragging === 'bri') { calcBriFromX(e.clientX); return }
  e.preventDefault(); updatePos(e, dragging)
}
function onMouseUp()   { if (dragging) emitChange(); dragging = null }
function startSatDrag(e) { dragging = 'sat'; updatePos(e.touches ? e.touches[0] : e, 'sat') }
function startHueDrag(e) { dragging = 'hue'; updatePos(e.touches ? e.touches[0] : e, 'hue') }
function startSpeedDrag(e) {
  if (cfg.effect === 0) return
  dragging = 'speed'
  const ev = e.touches ? e.touches[0] : e
  calcSpeedFromX(ev.clientX)
}
function startBriDrag(e) {
  dragging = 'bri'
  const ev = e.touches ? e.touches[0] : e
  calcBriFromX(ev.clientX)
}

// ===== HSV/RGB =====
function hsvToRgb(h, s, v) {
  s /= 100; v /= 100
  const c = v * s, x = c * (1 - Math.abs((h / 60) % 2 - 1)), m = v - c
  let r1, g1, b1
  if (h < 60)      { r1=c; g1=x; b1=0 }
  else if (h < 120) { r1=x; g1=c; b1=0 }
  else if (h < 180) { r1=0; g1=c; b1=x }
  else if (h < 240) { r1=0; g1=x; b1=c }
  else if (h < 300) { r1=x; g1=0; b1=c }
  else              { r1=c; g1=0; b1=x }
  return { r: Math.round((r1+m)*255), g: Math.round((g1+m)*255), b: Math.round((b1+m)*255) }
}

function rgbToHsv(r, g, b) {
  r /= 255; g /= 255; b /= 255
  const max = Math.max(r,g,b), min = Math.min(r,g,b), d = max - min
  let h = 0
  if (d !== 0) {
    if (max === r) h = ((g-b)/d + (g<b?6:0)) * 60
    else if (max === g) h = ((b-r)/d + 2) * 60
    else h = ((r-g)/d + 4) * 60
  }
  return { h: Math.round(h), s: Math.round(max===0?0:d/max*100), v: Math.round(max*100) }
}

function rgbToHex(r,g,b) { return [r,g,b].map(c => c.toString(16).padStart(2,'0')).join('').toUpperCase() }
function hexToRgb(hex) {
  hex = hex.replace('#','')
  if (hex.length !== 6) return null
  const r=parseInt(hex.substring(0,2),16), g=parseInt(hex.substring(2,4),16), b=parseInt(hex.substring(4,6),16)
  return (isNaN(r)||isNaN(g)||isNaN(b)) ? null : {r,g,b}
}

function syncFromHsv() {
  const rgb = hsvToRgb(hue.value, sat.value, bri.value)
  cfg.r = rgb.r; cfg.g = rgb.g; cfg.b = rgb.b
  saveDraft()
}
function syncFromRgb() {
  const hsv = rgbToHsv(cfg.r, cfg.g, cfg.b)
  hue.value = hsv.h; sat.value = hsv.s; bri.value = hsv.v
  saveDraft(); emitChange()
}

// ===== 去抖发送 + 自动保存 =====
let rafPending = false
let saveTimer = null

function emitChange() {
  if (rafPending) return
  rafPending = true
  requestAnimationFrame(() => {
    rafPending = false
    emit('setLedColor', selected.value, cfg.r, cfg.g, cfg.b, cfg.effect, cfg.brightness, cfg.interval)
  })
  // 800ms 无操作后自动保存到 NVS
  if (saveTimer) clearTimeout(saveTimer)
  saveTimer = setTimeout(() => emit('saveLedConfig'), 800)
}

// ===== UI 事件 =====
function onRgbInput() {
  cfg.r = clamp(cfg.r,0,255); cfg.g = clamp(cfg.g,0,255); cfg.b = clamp(cfg.b,0,255)
  syncFromRgb()
}
function onHexInput() {
  const cleaned = hexDisplay.value.replace(/[^0-9a-fA-F]/g,'').substring(0,6)
  if (cleaned.length === 6) { const rgb = hexToRgb(cleaned); if (rgb) { cfg.r=rgb.r; cfg.g=rgb.g; cfg.b=rgb.b; syncFromRgb() } }
}
function pickPreset(c)   { const rgb = hexToRgb(c); if (rgb) { cfg.r=rgb.r; cfg.g=rgb.g; cfg.b=rgb.b; syncFromRgb() } }
function isPresetSelected(c) { const rgb = hexToRgb(c); return rgb && rgb.r===cfg.r && rgb.g===cfg.g && rgb.b===cfg.b }
function clamp(v, min, max) { return v < min ? min : v > max ? max : v }

onMounted(() => {
  document.addEventListener('mousemove', onMouseMove)
  document.addEventListener('mouseup', onMouseUp)
  document.addEventListener('touchmove', (e) => {
    if (!dragging) return; e.preventDefault()
    const pos = e.touches[0]
    if (dragging === 'speed') { calcSpeedFromX(pos.clientX); return }
    if (dragging === 'bri') { calcBriFromX(pos.clientX); return }
    updatePos(pos, dragging)
  }, { passive: false })
  document.addEventListener('touchend', () => { if (dragging) emitChange(); dragging = null })
  if (props.connected) emit('requestLedConfig')
  loadMode(1)
})
onUnmounted(() => {
  document.removeEventListener('mousemove', onMouseMove)
  document.removeEventListener('mouseup', onMouseUp)
})
</script>

<style scoped>
.icon-solid {
  background: repeating-conic-gradient(currentColor 0% 25%, transparent 0% 50%) 50% / 8px 8px;
  border-radius: 2px;
}
.icon-breath {
  border: 2px solid currentColor;
  border-radius: 4px;
  opacity: 0.7;
}
.icon-blink {
  background: radial-gradient(circle, currentColor 30%, transparent 30%);
  background-size: 6px 6px;
  opacity: 0.8;
}
.icon-rainbow {
  background: repeating-linear-gradient(135deg,
    currentColor 0px, currentColor 2px,
    transparent 2px, transparent 4px);
  border-radius: 3px;
  opacity: 0.7;
}
</style>
