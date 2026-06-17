<template>
  <div class="map-root relative w-full h-[calc(100vh-8rem)] landscape:h-[calc(100vh-6rem)] rounded-2xl overflow-hidden border border-[var(--theme-border)] bg-darwin-panel">
    <!-- 地图容器 -->
    <div ref="mapContainer" class="absolute inset-0 z-0"></div>

    <!-- 顶部提示条: GPS 信号丢失 -->
    <div
      v-if="!hasGpsFix"
      class="absolute top-3 left-3 z-[1000] flex items-center gap-3 px-3 py-2 rounded-lg bg-darwin-panel/90 backdrop-blur border border-[var(--theme-border)] shadow-lg"
    >
      <span class="text-darwin-muted text-xs">🛰 {{ t.waitingGps || '等待 GPS 信号…' }}</span>
      <button
        type="button"
        @click="startDemoMode"
        class="px-3 py-1 text-xs font-bold rounded-full bg-darwin-amber/20 text-darwin-amber border border-darwin-amber/30 hover:bg-darwin-amber/30 transition-all"
      >
        {{ t.demo || '模拟数据' }}
      </button>
    </div>

    <!-- 网络提示: 瓦片加载失败 -->
    <div
      v-if="tileLoadFailed"
      class="absolute top-16 left-3 z-[1000] flex items-center gap-2 px-3 py-2 rounded-lg bg-orange-500/20 backdrop-blur border border-orange-500/30 shadow-lg max-w-[calc(100%-6rem)]"
    >
      <span class="text-orange-300 text-[0.65rem] leading-tight">{{ t.networkHint || '无法加载地图瓦片，请开启手机“无线局域网助理”(iOS) 或“智能网络切换”(Android) 后重试' }}</span>
    </div>

    <!-- 右上角控制栏 -->
    <div class="absolute top-3 right-3 z-[1000] flex flex-col gap-2">
      <!-- 自动跟随切换 -->
      <button
        type="button"
        @click="toggleFollow"
        :class="['px-3 py-1.5 text-xs font-bold rounded-lg backdrop-blur border shadow-lg transition-all',
                 autoFollow
                   ? 'bg-darwin-amber/20 text-darwin-amber border-darwin-amber/30'
                   : 'bg-darwin-panel/90 text-darwin-muted border-[var(--theme-border)]']"
      >
        {{ autoFollow ? (t.following || '追踪中') : (t.follow || '跟随') }}
      </button>

      <!-- 居中按钮 -->
      <button
        type="button"
        @click="centerOnAircraft"
        class="px-3 py-1.5 text-xs font-bold rounded-lg bg-darwin-panel/90 backdrop-blur border border-[var(--theme-border)] text-darwin-muted hover:text-darwin-ink shadow-lg transition-all"
      >
        ⊙ {{ t.center || '居中' }}
      </button>

      <!-- 轨迹切换 -->
      <button
        type="button"
        @click="showTrajectory = !showTrajectory"
        :class="['px-3 py-1.5 text-xs font-bold rounded-lg backdrop-blur border shadow-lg transition-all',
                 showTrajectory
                   ? 'bg-darwin-amber/20 text-darwin-amber border-darwin-amber/30'
                   : 'bg-darwin-panel/90 text-darwin-muted border-[var(--theme-border)]']"
      >
        {{ showTrajectory ? (t.trailOn || '轨迹开') : (t.trailOff || '轨迹关') }}
      </button>

      <!-- 清除轨迹 -->
      <button
        v-if="showTrajectory && trajectoryPoints.length > 0"
        type="button"
        @click="clearTrajectory"
        class="px-3 py-1.5 text-xs font-bold rounded-lg bg-red-500/20 text-red-400 border border-red-500/30 shadow-lg transition-all hover:bg-red-500/30"
      >
        ✕ {{ t.clearTrail || '清除' }}
      </button>
    </div>

    <!-- 底部状态栏 -->
    <div
      v-if="hasGpsFix && telemetry"
      class="absolute bottom-3 left-3 right-3 z-[1000] flex flex-wrap gap-2 px-3 py-2 rounded-lg bg-darwin-panel/90 backdrop-blur border border-[var(--theme-border)] shadow-lg text-xs"
    >
      <span class="text-darwin-muted">
        🛰 {{ telemetry.gps.sats }}S
      </span>
      <span class="text-darwin-muted">
        {{ t.alt || '高度' }}: {{ telemetry.gps.altitude.toFixed(0) }}m
      </span>
      <span class="text-darwin-muted">
        {{ t.speed || '速度' }}: {{ (telemetry.gps.speed * 3.6).toFixed(0) }}km/h
      </span>
      <span class="text-darwin-muted">
        {{ t.heading || '航向' }}: {{ telemetry.gps.heading.toFixed(0) }}°
      </span>
      <span class="text-darwin-muted/60 ml-auto">
        {{ telemetry.gps.latitude.toFixed(6) }}, {{ telemetry.gps.longitude.toFixed(6) }}
      </span>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch, onMounted, onUnmounted, nextTick } from 'vue'
import L from 'leaflet'
import 'leaflet/dist/leaflet.css'

const props = defineProps({
  telemetry: { type: Object, default: null },
  t: { type: Object, default: () => ({}) },
})

const LOCAL_TILE_URL = 'http://192.168.4.1/tiles/{z}/{x}/{y}.png'
const autoFollow = ref(true)
const showTrajectory = ref(true)
const trajectoryPoints = ref([])
const MAX_TRAIL_POINTS = 2000
const mapContainer = ref(null)
let map = null
let tileLayer = null
let aircraftMarker = null
let trajectoryLine = null

// 计算是否有 GPS 定位
const hasGpsFix = computed(() => {
  return props.telemetry?.gps?.sats > 0 &&
         props.telemetry?.gps?.latitude != null &&
         props.telemetry?.gps?.longitude != null
})

const gpsInfo = computed(() => {
  if (!props.telemetry?.gps) return ''
  const sats = props.telemetry.gps.sats ?? 0
  return sats > 0 ? `${sats} 颗卫星` : ''
})

function createAircraftMarker(lat, lon) {
  return L.circleMarker([lat, lon], {
    radius: 8,
    color: '#22c55e',
    fillColor: '#22c55e',
    fillOpacity: 0.9,
    weight: 2,
    opacity: 1,
    zIndexOffset: 1000,
  })
}

function initMap() {
  if (!mapContainer.value) return
  if (map) return

  // 默认中心 (华盛顿阿灵顿 — 五角大楼附近)
  const defaultCenter = [38.91046, -77.01621]

  map = L.map(mapContainer.value, {
    center: defaultCenter,
    zoom: 16,
    minZoom: 12,
    maxZoom: 17,
    zoomControl: false,
    attributionControl: false,
  })

  // 缩放控件放到左下角
  L.control.zoom({ position: 'bottomleft' }).addTo(map)

  // 离线地图瓦片 (从 ESP32 TF 卡 HTTP 服务加载, SASPlanet TMS 格式)
  tileLayer = L.tileLayer(LOCAL_TILE_URL, {
    attribution: '',
    subdomains: [],
    maxZoom: 17,
    keepBuffer: 5,
    tms: true,  // SASPlanet 导出为 TMS 格式 (Y 轴与 XYZ 相反)
  }).addTo(map)

  // 飞行轨迹 Polyline (初始隐藏)
  trajectoryLine = L.polyline([], {
    color: '#22c55e',
    weight: 3,
    opacity: 0.7,
    smoothFactor: 1,
  })
  if (showTrajectory.value) trajectoryLine.addTo(map)

  // 飞机圆形标记 (先放在默认中心, GPS 到后更新)
  aircraftMarker = createAircraftMarker(defaultCenter[0], defaultCenter[1])
  aircraftMarker.addTo(map)

  // 拖动地图时关闭自动跟随
  map.on('dragstart', () => {
    autoFollow.value = false
  })

  // 延迟修复尺寸 (解决容器刚显示时的渲染问题)
  setTimeout(() => map.invalidateSize(), 200)
}

function toggleFollow() {
  autoFollow.value = !autoFollow.value
  if (autoFollow.value && hasGpsFix.value) {
    centerOnAircraft()
  }
}

function clearTrajectory() {
  trajectoryPoints.value = []
  if (trajectoryLine) {
    trajectoryLine.setLatLngs([])
  }
}

watch(showTrajectory, (on) => {
  if (!trajectoryLine || !map) return
  if (on) {
    trajectoryLine.addTo(map)
  } else {
    map.removeLayer(trajectoryLine)
  }
})

function centerOnAircraft() {
  if (!map || !hasGpsFix.value) return
  const { latitude, longitude } = props.telemetry.gps
  map.setView([latitude, longitude], map.getZoom())
  autoFollow.value = true
}

function startDemoMode() {
  localStorage.setItem('mock_gps', 'true')
  window.location.reload()
}

// 监听 telemetry 更新飞机位置
watch(
  () => props.telemetry?.gps,
  (gps) => {
    if (!map || !aircraftMarker || !gps || !gps.sats) return
    const { latitude, longitude, heading } = gps
    if (latitude == null || longitude == null) return

    // 更新飞机标记位置
    aircraftMarker.setLatLng([latitude, longitude])

    // 自动跟随
    if (autoFollow.value) {
      map.setView([latitude, longitude], map.getZoom(), { animate: true })
    }

    // 飞行轨迹: 累加点 (从当前位置开始)
    if (!trajectoryLine) return
    trajectoryPoints.value.push([latitude, longitude])
    if (trajectoryPoints.value.length > MAX_TRAIL_POINTS) {
      trajectoryPoints.value.splice(0, trajectoryPoints.value.length - MAX_TRAIL_POINTS)
    }
    trajectoryLine.setLatLngs(trajectoryPoints.value)
  },
  { deep: true }
)

onMounted(() => {
  nextTick(() => initMap())
})

onUnmounted(() => {
  if (map) {
    map.remove()
    map = null
    tileLayer = null
    aircraftMarker = null
    trajectoryLine = null
  }
})
</script>

<style>
/* Leaflet 容器填满父元素 */
.leaflet-container {
  width: 100%;
  height: 100%;
  background: #1a1a2e;
}

/* 暗色主题 Leaflet 调整 */
.leaflet-control-zoom a {
  background: rgba(30, 30, 50, 0.9) !important;
  color: #ccc !important;
  border-color: rgba(255, 255, 255, 0.1) !important;
}
.leaflet-control-zoom a:hover {
  background: rgba(50, 50, 70, 0.9) !important;
  color: #fff !important;
}
</style>
