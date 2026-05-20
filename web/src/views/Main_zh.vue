<template>
  <div class="min-h-screen bg-[var(--theme-bg)] text-darwin-ink flex flex-col">
    <!-- ========== Top Nav Bar ========== -->
    <header class="sticky top-0 z-50 flex items-center h-14 px-2 sm:px-4 border-b border-[var(--theme-border)] bg-[var(--theme-header-bg)] backdrop-blur-md">
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
    <main class="flex-1 p-4 lg:p-6 max-w-6xl w-full mx-auto box-border">
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
            <div class="grid grid-cols-2 gap-3">
              <div v-for="ch in channels.slice(0, 8)" :key="ch.id" class="flex items-center justify-between p-3 border border-[var(--theme-border-light)] rounded-xl bg-[var(--theme-bg-subtle)]">
                <span class="text-darwin-muted text-xs font-bold">CH{{ ch.id }}</span>
                <div class="text-right">
                  <strong class="text-sm text-[var(--theme-text)] block">{{ ch.mappedValue }}</strong>
                  <span class="text-[0.6rem] text-darwin-muted">{{ percentLabel(ch.mappedValue) }}</span>
                </div>
              </div>
            </div>
          </article>
        </div>
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
            :t="configT"
            t-raw-label="原始值"
            @calibrate="showCalibrationModal = true"
            @save="saveCalibration(t)"
          />

          <!-- 通道映射 -->
          <ConfigChannelMapping
            v-if="configSubTab === 'mapping'"
            :mappings="channelMapping"
            :switch-options="availableSwitches"
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
            :t="t"
            @refresh="refreshCrsf"
            @bind="handleCrsfBind"
            @command="handleCrsfCommand"
            @select-change="handleCrsfSelectChange"
          />
        </div>
      </section>
    </main>

    <!-- WebSocket -->
    <WebSocketConnection
      ref="ws"
      @data="onWsData"
      @status="isConnected = $event"
      @connected="requestCalibration"
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

const {
  ws, isConnected, currentTab, configSubTab, currentLang, simMode,
  showCalibrationModal, stickMode, channelMapping, availableSwitches,
  isCrsfLoading, crsfStatus, channels, crsfMenus,
  leftStick, rightStick, visibleChannels,
  isDarkMode, toggleTheme,
  changeLanguage, saveCalibration, setStickMode,
  updateChannelMapping, resetChannelMapping, writeChannelMapping,
  refreshCrsf, handleCrsfBind, handleCrsfCommand, handleCrsfSelectChange,
  onCalibrationResult, onWsData, requestCalibration,
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
  notConnectedAlert: '未连接到设备，无法保存。',
  savedAlert: '模式与校准数据已发送。',
}

const configT = {
  autoCalibration: '开始校准',
  saveCalibration: '保存校准',
}

const mappingT = {
  write: '写入',
  reset: '还原',
  columnChannel: '通道',
  columnDefault: '默认值',
  columnCurrent: '当前值',
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

const navItems = computed(() => [
  { id: 'dashboard', label: '仪表盘' },
  { id: 'config', label: '配置' }
])

const configNavItems = computed(() => [
  { id: 'props', label: '通道属性' },
  { id: 'mapping', label: '通道映射' },
  { id: 'stick', label: '摇杆模式' },
  { id: 'crsf', label: 'CRSF' },
])

function percentLabel(val) {
  const pct = Math.round((val - 1500) / 5)
  return (pct > 0 ? '+' : '') + pct + '%'
}
</script>
