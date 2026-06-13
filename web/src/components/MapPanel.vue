<template>
  <div class="map-root relative w-full h-[calc(100vh-8rem)] landscape:h-[calc(100vh-6rem)] rounded-2xl overflow-hidden border border-[var(--theme-border)] bg-darwin-panel">
    <!-- 地图容器 -->
    <div ref="mapContainer" class="absolute inset-0 z-0"></div>

    <!-- 右上角控制栏 -->
    <div class="absolute top-3 right-3 z-[1000] flex flex-col gap-2">
      <!-- 地图源切换 -->
      <select
        v-model="tileSource"
        @change="switchTileLayer"
        class="px-3 py-1.5 text-xs font-bold rounded-lg bg-darwin-panel/90 backdrop-blur border border-[var(--theme-border)] text-darwin-ink outline-none cursor-pointer shadow-lg"
      >
        <option v-for="src in tileSources" :key="src.key" :value="src.key">{{ src.label }}</option>
      </select>

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

    <!-- GPS 丢失覆盖层 -->
    <div
      v-if="!hasGpsFix"
      class="absolute inset-0 z-[500] flex items-center justify-center bg-darwin-panel/80 backdrop-blur-sm"
    >
      <div class="text-center px-6 py-8 rounded-2xl bg-darwin-panel/90 border border-[var(--theme-border)] shadow-2xl">
        <div class="text-4xl mb-3">🛰</div>
        <p class="text-darwin-muted text-sm font-bold">{{ t.waitingGps || '等待 GPS 信号…' }}</p>
        <p class="text-darwin-muted/60 text-xs mt-1">{{ gpsInfo }}</p>
      </div>
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

// 瓦片源定义
const tileSources = [
  {
    key: 'gaode',
    label: '高德地图',
    url: 'https://webrd0{s}.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=1&style=8&x={x}&y={y}&z={z}',
    attribution: '&copy; 高德地图',
    subdomains: ['1', '2', '3', '4'],
  },
  {
    key: 'gaode_sat',
    label: '高德卫星',
    url: 'https://webst0{s}.is.autonavi.com/appmaptile?style=6&x={x}&y={y}&z={z}',
    attribution: '&copy; 高德地图',
    subdomains: ['1', '2', '3', '4'],
  },
  {
    key: 'osm',
    label: 'OpenStreetMap',
    url: 'https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',
    attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OSM</a>',
    subdomains: ['a', 'b', 'c'],
  },
  {
    key: 'arcgis_sat',
    label: 'ArcGIS 卫星',
    url: 'https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}',
    attribution: '&copy; Esri',
    subdomains: [],
  },
  {
    key: 'tianditu',
    label: '天地图',
    url: 'https://t0.tianditu.gov.cn/vec_w/wmts?SERVICE=WMTS&REQUEST=GetTile&VERSION=1.0.0&LAYER=vec&STYLE=default&TILEMATRIXSET=w&FORMAT=tiles&TILEMATRIX={z}&TILEROW={y}&TILECOL={x}&tk=',
    attribution: '&copy; 天地图',
    subdomains: [],
  },
]

const tileSource = ref('gaode')
const autoFollow = ref(true)
const showTrajectory = ref(true)
const trajectoryPoints = ref([])
const MAX_TRAIL_POINTS = 2000
const mapContainer = ref(null)
let map = null
let tileLayer = null
let aircraftMarker = null
let markerRotation = 0
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

function createAircraftIcon(heading) {
  // 带方向的飞机图标 (三角形)
  const size = 24
  const color = '#22c55e' // green-400
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 24 24" fill="${color}" transform="rotate(${heading}, 12, 12)">
    <path d="M12 2 L4 20 L12 15 L20 20 Z" stroke="white" stroke-width="1" stroke-linejoin="round"/>
    <circle cx="12" cy="2" r="2.5" fill="white"/>
  </svg>`
  return L.divIcon({
    html: svg,
    className: 'aircraft-marker',
    iconSize: [size, size],
    iconAnchor: [size / 2, size / 2],
  })
}

function initMap() {
  if (!mapContainer.value) return
  if (map) return

  // 默认中心 (深圳)
  const defaultCenter = [22.5, 114.0]

  map = L.map(mapContainer.value, {
    center: defaultCenter,
    zoom: 16,
    zoomControl: false,
    attributionControl: false,
  })

  // 缩放控件放到左下角
  L.control.zoom({ position: 'bottomleft' }).addTo(map)

  // 初始瓦片层
  const src = tileSources.find(s => s.key === tileSource.value)
  if (src) {
    tileLayer = L.tileLayer(src.url, {
      attribution: src.attribution,
      subdomains: src.subdomains,
      maxZoom: 19,
    }).addTo(map)
  }

  // 飞机标记
  aircraftMarker = L.marker(defaultCenter, {
    icon: createAircraftIcon(0),
    zIndexOffset: 1000,
  }).addTo(map)

  // 飞行轨迹 Polyline (初始隐藏)
  trajectoryLine = L.polyline([], {
    color: '#22c55e',
    weight: 3,
    opacity: 0.7,
    smoothFactor: 1,
  })
  if (showTrajectory.value) trajectoryLine.addTo(map)

  // 拖动地图时关闭自动跟随
  map.on('dragstart', () => {
    autoFollow.value = false
  })

  // 延迟修复尺寸 (解决容器刚显示时的渲染问题)
  setTimeout(() => map.invalidateSize(), 200)
}

function switchTileLayer() {
  if (!map) return
  const src = tileSources.find(s => s.key === tileSource.value)
  if (!src) return
  if (tileLayer) map.removeLayer(tileLayer)
  tileLayer = L.tileLayer(src.url, {
    attribution: src.attribution,
    subdomains: src.subdomains,
    maxZoom: 19,
  }).addTo(map)
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

// 监听 telemetry 更新飞机位置
watch(
  () => props.telemetry?.gps,
  (gps) => {
    if (!map || !aircraftMarker || !gps || !gps.sats) return
    const { latitude, longitude, heading } = gps
    if (latitude == null || longitude == null) return

    // 更新位置
    aircraftMarker.setLatLng([latitude, longitude])

    // 更新方向
    const hdg = heading ?? 0
    if (Math.abs(hdg - markerRotation) > 1) {
      markerRotation = hdg
      aircraftMarker.setIcon(createAircraftIcon(hdg))
    }

    // 自动跟随
    if (autoFollow.value) {
      map.setView([latitude, longitude], map.getZoom(), { animate: true })
    }

    // 飞行轨迹: 累加点
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

/* 自定义标记去除默认样式 */
.aircraft-marker {
  background: transparent !important;
  border: none !important;
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
