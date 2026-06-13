<template>
  <div class="min-h-screen bg-[var(--theme-bg)] text-darwin-ink flex flex-col">
    <!-- ========== Top Nav Bar ========== -->
    <header class="sticky top-0 z-50 flex items-center h-14 landscape:h-10 px-2 sm:px-4 border-b border-[var(--theme-border)] bg-[var(--theme-header-bg)] backdrop-blur-md">
      <!-- Left: Logo -->
      <div class="flex items-center gap-2 mr-3 sm:mr-6 shrink-0">
        <img src="/logo-icon.png" class="h-7 w-auto sm:hidden" alt="logo" />
        <img src="/logo-horizontal.png" class="hidden sm:block h-7 w-auto" alt="logo" />
      </div>

      <!-- Center: Nav Tabs -->
      <nav class="flex items-center gap-1">
        <button
          v-for="tab in navItems"
          :key="tab.id"
          type="button"
          @click="currentTab = tab.id"
          :class="['px-3 sm:px-4 py-1.5 text-sm font-bold rounded-full transition-all whitespace-nowrap', currentTab === tab.id ? 'bg-darwin-amber/20 text-darwin-amber' : 'text-darwin-muted hover:text-darwin-ink']"
        >
          {{ tab.label }}
        </button>
      </nav>

      <!-- Spacer -->
      <div class="flex-1"></div>

      <!-- Right: Connection Status + Theme + Lang -->
      <div class="flex items-center gap-1.5 sm:gap-3">
        <!-- Connection Status -->
        <div class="flex items-center gap-1.5 text-xs">
          <span class="inline-block w-2 h-2 rounded-full shrink-0" :class="isConnected ? 'bg-green-500' : 'bg-red-500'"></span>
          <span class="text-darwin-muted hidden sm:inline">{{ isConnected ? t.online : t.offline }}</span>
        </div>

        <!-- Theme Toggle -->
        <button
          type="button"
          @click="toggleTheme"
          class="w-7 h-7 flex items-center justify-center rounded-full text-sm transition-all hover:bg-[var(--theme-bg-hover)] text-darwin-muted hover:text-darwin-ink"
          :title="isDarkMode ? '浅色模式' : '深色模式'"
        >
          {{ isDarkMode ? '☀' : '☾' }}
        </button>

        <!-- Language Switch -->
        <div class="flex items-center border border-[var(--theme-border)] rounded-full overflow-hidden text-xs">
          <button
            type="button"
            @click="changeLanguage('zh')"
            :class="['px-2 sm:px-2.5 py-1 font-bold transition-all', currentLang === 'zh' ? 'bg-darwin-amber text-black' : 'text-darwin-muted']"
          >中</button>
          <button
            type="button"
            @click="changeLanguage('en')"
            :class="['px-2 sm:px-2.5 py-1 font-bold transition-all', currentLang === 'en' ? 'bg-darwin-amber text-black' : 'text-darwin-muted']"
          >EN</button>
        </div>
      </div>
    </header>

    <!-- ========== Main Content ========== -->
    <main class="flex-1 p-4 landscape:p-2 lg:p-6 max-w-6xl w-full mx-auto box-border">
      <!-- ===== Tab: Dashboard ===== -->
      <section v-if="currentTab === 'dashboard'" class="grid gap-5">
        <!-- Device Info Cards -->
        <div class="grid grid-cols-2 lg:grid-cols-4 gap-3">
          <article class="p-4 rounded-2xl border border-[var(--theme-border)] bg-darwin-panel">
            <span class="text-darwin-muted text-[0.7rem] uppercase tracking-widest block mb-1">{{ t.connection }}</span>
            <div class="flex items-center gap-2">
              <span class="inline-block w-2.5 h-2.5 rounded-full" :class="isConnected ? 'bg-green-500' : 'bg-red-500'"></span>
              <strong class="text-base" :class="isConnected ? 'text-green-400' : 'text-red-400'">{{ isConnected ? t.online : t.offline }}</strong>
            </div>
          </article>

          <article class="p-4 rounded-2xl border border-[var(--theme-border)] bg-darwin-panel">
            <span class="text-darwin-muted text-[0.7rem] uppercase tracking-widest block mb-1">{{ t.mode }}</span>
            <strong class="text-base text-darwin-amber">{{ simMode === 1 ? 'XBOX' : 'HID' }}</strong>
          </article>

          <article class="p-4 rounded-2xl border border-[var(--theme-border)] bg-darwin-panel">
            <span class="text-darwin-muted text-[0.7rem] uppercase tracking-widest block mb-1">CRSF</span>
            <strong class="text-base" :class="crsfStatus.isLinked ? 'text-green-400' : 'text-darwin-muted'">
              {{ crsfStatus.isLinked ? t.linked : t.unlinked }}
            </strong>
          </article>

          <article class="p-4 rounded-2xl border border-[var(--theme-border)] bg-darwin-panel">
            <span class="text-darwin-muted text-[0.7rem] uppercase tracking-widest block mb-1">{{ t.signal }}</span>
            <strong class="text-base text-darwin-ink">
              <template v-if="crsfStatus.isLinked">
                RSSI {{ crsfStatus.rssi ?? '--' }} · LQ {{ crsfStatus.lq ?? '--' }}
              </template>
              <span v-else class="text-darwin-muted">--</span>
            </strong>
          </article>
        </div>

        <!-- 按键模式设置 -->
        <article class="p-5 rounded-2xl border border-[var(--theme-border)] bg-darwin-panel">
          <h3 class="m-0 mb-4 text-darwin-amber text-[0.7rem] font-bold uppercase tracking-widest">{{ btnT.title }}</h3>
          <div class="grid grid-cols-4 gap-3">
            <div v-for="(btn, idx) in btnT.items" :key="idx" class="flex flex-col gap-1.5">
              <span class="text-[0.65rem] text-darwin-muted font-bold">{{ btn.name }}</span>
              <select
                :value="btnCfg[idx]"
                @change="setBtnCfg(idx, parseInt($event.target.value))"
                class="px-3 py-2 text-sm rounded-lg bg-darwin-panel border border-[var(--theme-border)] text-darwin-ink outline-none cursor-pointer"
              >
                <option v-for="opt in btn.options" :key="opt.val" :value="opt.val">{{ opt.label }}</option>
              </select>
            </div>
          </div>
          <div class="flex items-center gap-3 mt-3">
            <button type="button" @click="resetBtnCfg"
              class="px-4 py-1.5 text-xs font-bold rounded-full border border-[var(--theme-border)] text-darwin-muted"
            >{{ btnT.reset }}</button>
          </div>
        </article>

        <!-- Joystick + Main Channels -->
        <div class="grid grid-cols-1 lg:grid-cols-2 gap-4">
          <!-- Joystick Section -->
          <article class="p-5 rounded-2xl border border-[var(--theme-border)] bg-darwin-panel">
            <h3 class="m-0 mb-4 text-darwin-amber text-[0.7rem] font-bold uppercase tracking-widest">{{ t.stickView }}</h3>
            <div class="grid grid-cols-2 gap-4 justify-items-center">
              <Joystick :x="leftStick.x" :y="leftStick.y" :label="t.leftStick" />
              <Joystick :x="rightStick.x" :y="rightStick.y" :label="t.rightStick" />
            </div>
          </article>

          <!-- Channel Values Overview -->
          <article class="p-5 rounded-2xl border border-[var(--theme-border)] bg-darwin-panel">
            <h3 class="m-0 mb-4 text-darwin-amber text-[0.7rem] font-bold uppercase tracking-widest">{{ t.channelValues }}</h3>
            <div class="flex flex-col gap-1">
              <ChannelBar
                v-for="ch in visibleChannels"
                :key="ch.id"
                :channel-id="ch.id"
                :mapped-value="ch.mappedValue"
              />
            </div>
          </article>
        </div>

        <!-- 配置快照 -->
        <article class="p-5 rounded-2xl border border-[var(--theme-border)] bg-darwin-panel">
          <h3 class="m-0 mb-4 text-darwin-amber text-[0.7rem] font-bold uppercase tracking-widest">{{ profileT.title }}</h3>
          <ConfigProfiles
            :profiles="profiles"
            :t="profileT"
            :import-success="importSuccess"
            :import-error="importError"
            @save="saveProfile"
            @load="loadProfile"
            @delete="deleteProfile"
            @rename="renameProfile"
            @export-profile="exportProfile"
            @import="handleImport"
          />
        </article>
      </section>

      <!-- ===== Tab: Configuration ===== -->
      <section v-if="currentTab === 'config'" class="grid grid-cols-1 lg:grid-cols-[180px_minmax(0,1fr)] gap-5">
        <!-- Config Sidebar Nav -->
        <nav class="flex lg:flex-col gap-2 lg:sticky lg:top-20 lg:self-start">
          <button
            v-for="sub in configNavItems"
            :key="sub.id"
            type="button"
            @click="configSubTab = sub.id"
            :class="['px-4 py-2.5 text-sm font-bold text-left rounded-xl transition-all whitespace-nowrap lg:whitespace-normal', configSubTab === sub.id ? 'bg-darwin-amber/20 text-darwin-amber border border-darwin-amber/30' : 'text-darwin-muted border border-transparent hover:border-[var(--theme-border)]']"
          >
            {{ sub.label }}
          </button>
        </nav>

        <!-- Config Content -->
        <div>
          <!-- 通道属性 -->
          <ConfigChannelProps
            v-if="configSubTab === 'props'"
            :channels="visibleChannels"
            :epa-data="epaData"
            :rev-mask="revMask"
            t-raw-label="原始值"
            @update-epa="(ch, pos, neg) => setEpa(ch, pos, neg)"
            @toggle-rev="(ch) => setRev(ch, !((revMask >> (ch - 1)) & 1))"
          />

          <!-- 通道映射 -->
          <ConfigChannelMapping
            v-if="configSubTab === 'mapping'"
            :mappings="channelMapping"
            :switch-options="availableSwitches"
            :write-state="mapWriteState"
            :t="mappingT"
            @write="writeChannelMapping"
            @reset="resetChannelMapping"
            @update:mapping="updateChannelMapping"
          />

          <!-- 摇杆模式 -->
          <ConfigStickMode
            v-if="configSubTab === 'stick'"
            :stick-mode="stickMode"
            :t="stickT"
            @update:stickMode="setStickMode"
            @calibrate="showCalibrationModal = true"
          />

          <!-- CRSF 配置 -->
          <CrsfConfiguratorPanel
            v-if="configSubTab === 'crsf'"
            :menus="crsfMenus"
            :status="crsfStatus"
            :loading="isCrsfLoading"
            :bindState="bindState"
            :t="t"
            @refresh="refreshCrsf"
            @bind="handleCrsfBind"
            @command="handleCrsfCommand"
            @select-change="handleCrsfSelectChange"
          />
        </div>
      </section>

      <!-- ===== Tab: LED 灯效 ===== -->
      <section v-if="currentTab === 'led'">
        <LedConfiguratorPanel
          lang="zh"
          :led-config="ledConfig"
          :connected="isConnected"
          @set-led-color="setLedColor"
          @request-led-config="requestLedConfig"
          @save-led-config="saveLedConfig"
        />
      </section>

      <!-- ===== Tab: 遥测 ===== -->
      <section v-if="currentTab === 'telemetry'">
        <TelemetryPanel
          :telemetry="telemetry"
          :status="crsfStatus"
          :t="telemetryT"
        />
      </section>

      <!-- ===== Tab: 地图 ===== -->
      <section v-if="currentTab === 'map'" class="h-full">
        <MapPanel
          :telemetry="telemetry"
          :t="mapT"
        />
      </section>
    </main>

    <!-- WebSocket -->
    <WebSocketConnection
      ref="ws"
      @data="onWsData"
      @status="isConnected = $event"
      @connected="requestCalibration(); loadProfileList()"
    />

    <!-- Calibration Modal -->
    <CalibrationModal
      :visible="showCalibrationModal"
      :channels="channels"
      :t="calT"
      @close="showCalibrationModal = false"
      @calibrate="onCalibrationResult"
    />
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'
import { useRCState } from '../composables/useRCState.js'
import Joystick from '../components/Joystick.vue'
import WebSocketConnection from '../components/WebSocketConnection.vue'
import CalibrationModal from '../components/CalibrationModal.vue'
import CrsfConfiguratorPanel from '../components/CrsfConfiguratorPanel.vue'
import ConfigChannelProps from '../components/ConfigChannelProps.vue'
import ConfigChannelMapping from '../components/ConfigChannelMapping.vue'
import ConfigStickMode from '../components/ConfigStickMode.vue'
import ConfigProfiles from '../components/ConfigProfiles.vue'
import LedConfiguratorPanel from '../components/LedConfiguratorPanel.vue'
import ChannelBar from '../components/ChannelBar.vue'
import TelemetryPanel from '../components/TelemetryPanel.vue'
import MapPanel from '../components/MapPanel.vue'

const {
  ws, isConnected, currentTab, configSubTab, currentLang, simMode,
  showCalibrationModal, stickMode, channelMapping, availableSwitches,
  isCrsfLoading, bindState, crsfStatus, channels, crsfMenus,
  epaData, revMask,
  leftStick, rightStick, visibleChannels,
  btnCfg,
  isDarkMode, toggleTheme,
  changeLanguage, setStickMode,
  mapWriteState, updateChannelMapping, resetChannelMapping, writeChannelMapping,
  refreshCrsf, handleCrsfBind, handleCrsfCommand, handleCrsfSelectChange,
  onCalibrationResult, onWsData, requestCalibration,
  setEpa, setRev,
  setBtnCfg, resetBtnCfg,
  profiles, loadProfileList, saveProfile, loadProfile, deleteProfile,
  renameProfile, exportConfig, importConfig,
  importSuccess, importError, exportProfile,
  ledConfig, requestLedConfig, setLedColor, saveLedConfig,
  telemetry,
} = useRCState()

currentLang.value = 'zh'

const t = {
  online: '在线',
  offline: '离线',
  connection: '连接状态',
  mode: '模式',
  linked: '已链路',
  unlinked: '未链路',
  signal: '信号',
  stickView: '摇杆视图',
  leftStick: '左摇杆',
  rightStick: '右摇杆',
  channelValues: '通道数值',
}

const configT = {}

const profileT = {
  title: '配置快照',
  namePlaceholder: '方案名称...',
  save: '保存',
  export: '导出',
  import_: '导入',
  load: '加载',
  delete: '删除',
  rename: '重命名',
  empty: '暂无已保存的方案',
  confirmPrompt: '加载此方案？',
  confirmLoad: '加载',
  confirmDel: '删除此方案？',
  confirmDelete: '删除',
  confirmRename: '确认',
  confirmOverwrite: '方案已存在，覆盖？',
  confirmOverwriteBtn: '覆盖',
  importSuccess: '导入成功',
  cancel: '取消',
}

function handleImport(jsonStr) {
  importConfig(jsonStr)
}

const mappingT = {
  write: '写入',
  reset: '还原',
  columnChannel: '通道',
  columnDefault: '默认值',
  columnCurrent: '当前值',
  confirmWrite: '确认写入？',
  confirmReset: '确认还原？',
  confirmWriteDesc: '将当前通道映射写入设备',
  confirmResetDesc: '恢复为默认通道映射',
  confirmOk: '确定',
  confirmCancel: '取消',
  sendingWrite: '写入中...',
  sendingReset: '还原中...',
  doneWrite: '写入完成',
  doneReset: '还原完成',
}

const stickT = {
  stickMode: '摇杆模式',
  mode2: '美国手',
  mode1: '日本手',
  mode2Desc: '左摇杆: 偏航+油门 / 右摇杆: 横滚+俯仰',
  mode1Desc: '左摇杆: 俯仰+偏航 / 右摇杆: 横滚+油门',
  leftStick: '左摇杆',
  rightStick: '右摇杆',
  calibrate: '摇杆校准',
}

const btnT = {
  title: '按键模式设置',
  reset: '还原默认',
  items: [
    {
      name: 'SA',
      options: [
        { val: 0, label: '触发' },
        { val: 1, label: '单击' },
        { val: 2, label: '双击' },
      ],
    },
    {
      name: 'SB',
      options: [
        { val: 0, label: '三态' },
        { val: 1, label: '二态' },
      ],
    },
    {
      name: 'SC',
      options: [
        { val: 0, label: '三态' },
        { val: 1, label: '二态' },
      ],
    },
    {
      name: 'SD',
      options: [
        { val: 0, label: '触发' },
        { val: 1, label: '单击' },
        { val: 2, label: '双击' },
      ],
    },
  ],
}

const calT = {
  calDetect: '持续轻推要校准的摇杆',
  calDetectHint: '等待系统自动识别...',
  calPushMax: '将摇杆推到上方最大值处',
  calPushMaxHint: '到达上限后松手，将进入下一步',
  calRotate: '以最大角度顺时针转动摇杆 3 圈',
  calRotateHint: '已转动',
  calRotateUnit: '圈',
  calComplete: '校准完成！',
  calAxisLeftX: '左 X (偏航)',
  calAxisLeftY: '左 Y (油门)',
  calAxisRightX: '右 X (横滚)',
  calAxisRightY: '右 Y (俯仰)'
}

const telemetryT = {
  flightMode: '飞行模式',
  battery: '电池',
  sats: '颗卫星',
  fixed: '已锁定',
  noGps: '等待 GPS 定位',
  noData: '等待数据',
  linkQuality: '链路质量',
  disconnected: '未连接',
  attitude: '飞行姿态',
  gpsDetail: 'GPS 位置',
  altitude: '高度',
  speed: '速度',
  heading: '航向',
  waitingGps: '等待 GPS 定位…',
  barometer: '气压计',
  baroAlt: '气压高度',
  vSpeed: '垂直速度',
  lastUpdate: '最后更新',
}

const mapT = {
  waitingGps: '等待 GPS 信号…',
  follow: '跟随',
  following: '追踪中',
  center: '居中',
  alt: '高度',
  speed: '速度',
  heading: '航向',
  trailOn: '轨迹开',
  trailOff: '轨迹',
  clearTrail: '清除',
}

const navItems = computed(() => [
  { id: 'dashboard', label: '仪表盘' },
  { id: 'config', label: '配置' },
  { id: 'map', label: '地图' },
  { id: 'led', label: '灯效' },
  { id: 'telemetry', label: '遥测' },
])

const configNavItems = computed(() => [
  { id: 'props', label: '通道属性' },
  { id: 'mapping', label: '通道映射' },
  { id: 'stick', label: '摇杆模式' },
  { id: 'crsf', label: 'CRSF' },
])


</script>
