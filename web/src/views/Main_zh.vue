<template>
  <div class="min-h-screen bg-black text-darwin-ink flex flex-col">
    <!-- ========== Top Nav Bar ========== -->
    <header class="sticky top-0 z-50 flex items-center h-14 px-4 border-b border-white/10 bg-black/90 backdrop-blur-md">
      <!-- Left: Icon -->
      <div class="flex items-center gap-2 mr-6 shrink-0">
        <img src="data:image/avif;base64,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" class="h-7 w-auto" alt="logo" />
      </div>

      <!-- Center: Nav Tabs -->
      <nav class="flex items-center gap-1">
        <button
          v-for="tab in navItems"
          :key="tab.id"
          type="button"
          @click="currentTab = tab.id"
          :class="['px-4 py-1.5 text-sm font-bold rounded-full transition-all', currentTab === tab.id ? 'bg-darwin-amber/20 text-darwin-amber' : 'text-darwin-muted hover:text-darwin-ink']"
        >
          {{ tab.label }}
        </button>
      </nav>

      <!-- Spacer -->
      <div class="flex-1"></div>

      <!-- Right: Connection Status + Lang -->
      <div class="flex items-center gap-3">
        <!-- Connection Status -->
        <div class="flex items-center gap-1.5 text-xs">
          <span class="inline-block w-2 h-2 rounded-full" :class="isConnected ? 'bg-green-500' : 'bg-red-500'"></span>
          <span class="text-darwin-muted">{{ isConnected ? t.online : t.offline }}</span>
        </div>

        <!-- Language Switch -->
        <div class="flex items-center border border-white/10 rounded-full overflow-hidden text-xs">
          <button
            type="button"
            @click="changeLanguage('zh')"
            :class="['px-2.5 py-1 font-bold transition-all', currentLang === 'zh' ? 'bg-darwin-amber text-black' : 'text-darwin-muted']"
          >中</button>
          <button
            type="button"
            @click="changeLanguage('en')"
            :class="['px-2.5 py-1 font-bold transition-all', currentLang === 'en' ? 'bg-darwin-amber text-black' : 'text-darwin-muted']"
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
          <article class="p-4 rounded-2xl border border-white/10 bg-darwin-panel">
            <span class="text-darwin-muted text-[0.7rem] uppercase tracking-widest block mb-1">{{ t.connection }}</span>
            <div class="flex items-center gap-2">
              <span class="inline-block w-2.5 h-2.5 rounded-full" :class="isConnected ? 'bg-green-500' : 'bg-red-500'"></span>
              <strong class="text-base" :class="isConnected ? 'text-green-400' : 'text-red-400'">{{ isConnected ? t.online : t.offline }}</strong>
            </div>
          </article>

          <article class="p-4 rounded-2xl border border-white/10 bg-darwin-panel">
            <span class="text-darwin-muted text-[0.7rem] uppercase tracking-widest block mb-1">{{ t.mode }}</span>
            <strong class="text-base text-darwin-amber">{{ simMode === 1 ? 'XBOX' : 'HID' }}</strong>
          </article>

          <article class="p-4 rounded-2xl border border-white/10 bg-darwin-panel">
            <span class="text-darwin-muted text-[0.7rem] uppercase tracking-widest block mb-1">CRSF</span>
            <strong class="text-base" :class="crsfStatus.isLinked ? 'text-green-400' : 'text-darwin-muted'">
              {{ crsfStatus.isLinked ? t.linked : t.unlinked }}
            </strong>
          </article>

          <article class="p-4 rounded-2xl border border-white/10 bg-darwin-panel">
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
          <article class="p-5 rounded-2xl border border-white/10 bg-darwin-panel">
            <h3 class="m-0 mb-4 text-darwin-amber text-[0.7rem] font-bold uppercase tracking-widest">{{ t.stickView }}</h3>
            <div class="grid grid-cols-2 gap-4 justify-items-center">
              <Joystick :x="leftStick.x" :y="leftStick.y" :label="t.leftStick" />
              <Joystick :x="rightStick.x" :y="rightStick.y" :label="t.rightStick" />
            </div>
          </article>

          <!-- Channel Values Overview -->
          <article class="p-5 rounded-2xl border border-white/10 bg-darwin-panel">
            <h3 class="m-0 mb-4 text-darwin-amber text-[0.7rem] font-bold uppercase tracking-widest">{{ t.channelValues }}</h3>
            <div class="grid grid-cols-2 gap-3">
              <div v-for="ch in channels.slice(0, 8)" :key="ch.id" class="flex items-center justify-between p-3 border border-white/5 rounded-xl bg-white/5">
                <span class="text-darwin-muted text-xs font-bold">CH{{ ch.id }}</span>
                <div class="text-right">
                  <strong class="text-sm text-white block">{{ ch.mappedValue }}</strong>
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
            :class="['px-4 py-2.5 text-sm font-bold text-left rounded-xl transition-all whitespace-nowrap lg:whitespace-normal', configSubTab === sub.id ? 'bg-darwin-amber/20 text-darwin-amber border border-darwin-amber/30' : 'text-darwin-muted border border-transparent hover:border-white/10']"
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
