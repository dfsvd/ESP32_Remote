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
          :title="isDarkMode ? 'Light Mode' : 'Dark Mode'"
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

        <!-- Key Mode Settings -->
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

        <!-- Config Snapshots -->
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
          <ConfigChannelProps
            v-if="configSubTab === 'props'"
            :channels="visibleChannels"
            :epa-data="epaData"
            :rev-mask="revMask"
            t-raw-label="RAW"
            @update-epa="(ch, pos, neg) => setEpa(ch, pos, neg)"
            @toggle-rev="(ch) => setRev(ch, !((revMask >> (ch - 1)) & 1))"
          />

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
            :bindState="bindState"
            :t="t"
            @refresh="refreshCrsf"
            @bind="handleCrsfBind"
            @command="handleCrsfCommand"
            @select-change="handleCrsfSelectChange"
          />
        </div>
      </section>

      <!-- ===== Tab: LED ===== -->
      <section v-if="currentTab === 'led'">
        <LedConfiguratorPanel
          lang="en"
          :led-config="ledConfig"
          :connected="isConnected"
          @set-led-color="setLedColor"
          @request-led-config="requestLedConfig"
          @save-led-config="saveLedConfig"
        />
      </section>

      <!-- ===== Tab: Telemetry ===== -->
      <section v-if="currentTab === 'telemetry'">
        <TelemetryPanel
          :telemetry="telemetry"
          :status="crsfStatus"
          :t="telemetryT"
        />
      </section>

      <!-- ===== Tab: Map ===== -->
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
import Joystick from '../components_en/Joystick.vue'
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
}

const configT = {}

const profileT = {
  title: 'Config Snapshots',
  namePlaceholder: 'Profile name...',
  save: 'Save',
  export: 'Export',
  import_: 'Import',
  load: 'Load',
  delete: 'Delete',
  rename: 'Rename',
  empty: 'No saved profiles',
  confirmPrompt: 'Load this profile?',
  confirmLoad: 'Load',
  confirmDel: 'Delete this profile?',
  confirmDelete: 'Delete',
  confirmRename: 'Save',
  confirmOverwrite: 'Profile exists, overwrite?',
  confirmOverwriteBtn: 'Overwrite',
  importSuccess: 'Import successful',
  cancel: 'Cancel',
}

function handleImport(jsonStr) {
  importConfig(jsonStr)
}

const mappingT = {
  write: 'Write',
  reset: 'Reset',
  columnChannel: 'Channel',
  columnDefault: 'Default',
  columnCurrent: 'Current',
  confirmWrite: 'Confirm Write?',
  confirmReset: 'Confirm Reset?',
  confirmWriteDesc: 'Write current channel mapping to device',
  confirmResetDesc: 'Restore default channel mapping',
  confirmOk: 'OK',
  confirmCancel: 'Cancel',
  sendingWrite: 'Writing...',
  sendingReset: 'Resetting...',
  doneWrite: 'Write Complete',
  doneReset: 'Reset Complete',
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

const btnT = {
  title: 'Key Mode',
  reset: 'Reset',
  items: [
    {
      name: 'SA',
      options: [
        { val: 0, label: 'Touch' },
        { val: 1, label: 'Single' },
        { val: 2, label: 'Double' },
      ],
    },
    {
      name: 'SB',
      options: [
        { val: 0, label: '3-state' },
        { val: 1, label: '2-state' },
      ],
    },
    {
      name: 'SC',
      options: [
        { val: 0, label: '3-state' },
        { val: 1, label: '2-state' },
      ],
    },
    {
      name: 'SD',
      options: [
        { val: 0, label: 'Touch' },
        { val: 1, label: 'Single' },
        { val: 2, label: 'Double' },
      ],
    },
  ],
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

const telemetryT = {
  flightMode: 'Flight Mode',
  battery: 'Battery',
  sats: 'Sats',
  fixed: '3D Fix',
  noGps: 'No GPS',
  noData: 'Waiting…',
  linkQuality: 'Link Quality',
  disconnected: 'Disconnected',
  attitude: 'Attitude',
  gpsDetail: 'GPS Position',
  altitude: 'Alt',
  speed: 'Speed',
  heading: 'Heading',
  waitingGps: 'Waiting for GPS…',
  barometer: 'Barometer',
  baroAlt: 'Baro Alt',
  vSpeed: 'V.Speed',
  lastUpdate: 'Last Update',
}

const mapT = {
  waitingGps: 'Waiting for GPS…',
  follow: 'Follow',
  following: 'Following',
  center: 'Center',
  alt: 'Alt',
  speed: 'Speed',
  heading: 'Hdg',
  trailOn: 'Trail ON',
  trailOff: 'Trail',
  clearTrail: 'Clear',
  gaode: 'Gaode',
  gaodeSat: 'Gaode Sat',
  osm: 'OpenStreetMap',
  arcgisSat: 'ArcGIS Sat',
  tianditu: 'Tianditu',
}

const navItems = computed(() => [
  { id: 'dashboard', label: 'Dashboard' },
  { id: 'config', label: 'Config' },
  { id: 'map', label: 'Map' },
  { id: 'led', label: 'LED' },
  { id: 'telemetry', label: 'Telemetry' },
])

const configNavItems = computed(() => [
  { id: 'props', label: 'Channel Props' },
  { id: 'mapping', label: 'Mapping' },
  { id: 'stick', label: 'Stick Mode' },
  { id: 'crsf', label: 'CRSF' },
])


</script>
