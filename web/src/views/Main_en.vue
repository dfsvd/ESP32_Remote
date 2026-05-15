<template>
  <div class="min-h-screen bg-black text-darwin-ink flex flex-col">
    <!-- ========== Top Nav Bar ========== -->
    <header class="sticky top-0 z-50 flex items-center h-14 px-4 border-b border-white/10 bg-black/90 backdrop-blur-md">
      <!-- Left: Icon -->
      <div class="flex items-center gap-2 mr-6 shrink-0">
        <img :src="logoUrl" class="h-7 w-auto" alt="logo" />
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
          <ConfigChannelProps
            v-if="configSubTab === 'props'"
            :channels="visibleChannels"
            :t="configT"
            t-raw-label="RAW"
            @calibrate="showCalibrationModal = true"
            @save="saveCalibration(t)"
          />

          <ConfigChannelMapping
            v-if="configSubTab === 'mapping'"
            :mappings="channelMapping"
            :switch-options="availableSwitches"
            :t="mappingT"
            @write="writeChannelMapping"
            @reset="resetChannelMapping"
            @update:mapping="updateChannelMapping"
          />

          <ConfigStickMode
            v-if="configSubTab === 'stick'"
            :stick-mode="stickMode"
            :t="stickT"
            @update:stickMode="setStickMode"
            @calibrate="showCalibrationModal = true"
          />

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
import Joystick from '../components_en/Joystick.vue'
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

currentLang.value = 'en'

const t = {
  online: 'Online',
  offline: 'Offline',
  connection: 'Connection',
  mode: 'Mode',
  linked: 'Linked',
  unlinked: 'Unlinked',
  signal: 'Signal',
  stickView: 'Stick View',
  leftStick: 'Left Stick',
  rightStick: 'Right Stick',
  channelValues: 'Channel Values',
  notConnectedAlert: 'Not connected, cannot save.',
  savedAlert: 'Mode and calibration data sent.',
}

const configT = {
  autoCalibration: 'Auto Calibrate',
  saveCalibration: 'Save Calibration',
}

const mappingT = {
  write: 'Write',
  reset: 'Reset',
  columnChannel: 'Channel',
  columnDefault: 'Default',
  columnCurrent: 'Current',
}

const stickT = {
  stickMode: 'Stick Mode',
  mode2: 'Mode 2',
  mode1: 'Mode 1',
  mode2Desc: 'Left: Yaw+Throttle / Right: Roll+Pitch',
  mode1Desc: 'Left: Pitch+Yaw / Right: Roll+Throttle',
  leftStick: 'Left Stick',
  rightStick: 'Right Stick',
  calibrate: 'Calibrate Sticks',
}

const calT = {
  calDetect: 'Gently nudge the stick',
  calDetectHint: 'Waiting for detection...',
  calPushMax: 'Push the stick to max',
  calPushMaxHint: 'Release to proceed',
  calRotate: 'Rotate the stick clockwise 3 times',
  calRotateHint: 'Rotations',
  calRotateUnit: '',
  calComplete: 'Calibration complete!',
  calAxisLeftX: 'Left X (Yaw)',
  calAxisLeftY: 'Left Y (Throttle)',
  calAxisRightX: 'Right X (Roll)',
  calAxisRightY: 'Right Y (Pitch)'
}

const navItems = computed(() => [
  { id: 'dashboard', label: 'Dashboard' },
  { id: 'config', label: 'Config' }
])

const configNavItems = computed(() => [
  { id: 'props', label: 'Channel Props' },
  { id: 'mapping', label: 'Mapping' },
  { id: 'stick', label: 'Stick Mode' },
  { id: 'crsf', label: 'CRSF' },
])

function percentLabel(val) {
  const pct = Math.round((val - 1500) / 5)
  return (pct > 0 ? '+' : '') + pct + '%'
}
</script>
