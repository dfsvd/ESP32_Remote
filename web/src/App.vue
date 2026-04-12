<script setup>
import { computed, ref } from 'vue';
import WebSocketConnection from './components/WebSocketConnection.vue';
import Joystick from './components/Joystick.vue';
import AuxChannelSlider from './components/AuxChannelSlider.vue';
import CrsfConfiguratorPanel from './components/CrsfConfiguratorPanel.vue';
import { mockCrsfMenus, mockCrsfStatus } from './mockCrsfState';

const wsConnection = ref(null);
const incomingDataBuffer = ref('');
const isSocketConnected = ref(false);
const activePage = ref('controls');

const SIM_MODE_DEFAULT = 0;
const SIM_MODE_XBOX = 1;

const simMode = ref(SIM_MODE_DEFAULT);
const crsfLoading = ref(false);
const crsfStatus = ref({ ...mockCrsfStatus });
const hasLiveCrsfData = ref(false);
const autoCalibrationState = ref('idle');
const autoCalibrationDraft = ref({});

const channels = ref(Array.from({ length: 16 }, (_, index) => ({
  id: index + 1,
  mappedValue: 1500,
  rawValue: 2048,
  cal: { min: 1000, mid: 1500, max: 2000 },
})));

const pages = [
  {
    id: 'controls',
    label: 'RC 面板',
    eyebrow: '控制台',
    title: '摇杆、通道与校准',
  },
  {
    id: 'crsf',
    label: 'CRSF 菜单',
    eyebrow: '参数配置',
    title: '接收机配置与命令入口',
  },
];

const menuNameTranslations = {
  'Packet Rate': '发包速率',
  'Telem Ratio': '回传比例',
  'Switch Mode': '开关模式',
  'Link Mode': '链路模式',
  'Model Match': '模型匹配',
  'TX Power (100mW)': '发射功率（100mW）',
  'Max Power': '最大发射功率',
  Dynamic: '动态功率',
  'VTX Administrator': '图传设置',
  Band: '频段',
  Channel: '频道',
  'Pwr Lvl': '功率等级',
  Pitmode: '陷波模式',
  'Send VTx': '发送图传设置',
  'WiFi Connectivity': 'WiFi 连接',
  'Enable WiFi': '开启 WiFi',
  'Enable Rx WiFi': '开启接收机 WiFi',
  Bind: '对频',
  'Bad/Good': '坏包/好包',
  'Root Menu': '根菜单',
};

function translateMenuName(name) {
  if (!name) {
    return name;
  }
  return menuNameTranslations[name] ?? name;
}

function normalizeCrsfMenuItem(item) {
  return {
    id: Number(item.id),
    parentId: Number(item.parentId ?? item.parent_id ?? 0),
    type: item.type ?? item.kind ?? 0,
    name: translateMenuName(item.name ?? `项目 ${item.id}`),
    value: Number.isFinite(Number(item.value)) ? Number(item.value) : 0,
    options: item.options ?? item.content ?? '',
  };
}

const crsfMenus = ref(mockCrsfMenus.map(normalizeCrsfMenuItem));

function applyCalibrationLine(line) {
  const entries = line.slice(2).split(';');
  entries.forEach(entry => {
    const [id, min, mid, max] = entry.split(',').map(value => parseInt(value.trim(), 10));
    const channel = channels.value.find(item => item.id === id);

    if (channel && !Number.isNaN(min) && !Number.isNaN(mid) && !Number.isNaN(max)) {
      channel.cal.min = min;
      channel.cal.mid = mid;
      channel.cal.max = max;
    }
  });
}

function applyModeLine(line) {
  const modePrefixes = ['M:', 'MODE:', 'SIM_MODE:'];
  const matchedPrefix = modePrefixes.find(prefix => line.startsWith(prefix));

  if (!matchedPrefix) {
    return false;
  }

  const nextMode = parseInt(line.slice(matchedPrefix.length).trim(), 10);
  if (nextMode === SIM_MODE_DEFAULT || nextMode === SIM_MODE_XBOX) {
    simMode.value = nextMode;
  }

  return true;
}

function applyRealtimeLine(line) {
  const channelStrings = line
    .split(',')
    .map(item => item.trim())
    .filter(Boolean);

  if (channelStrings.length !== channels.value.length) {
    return;
  }

  channelStrings.forEach((channelString, index) => {
    const channel = channels.value[index];
    if (!channel) {
      return;
    }

    const parts = channelString.split(':');
    if (parts.length === 2) {
      const mappedValue = parseInt(parts[0].trim(), 10);
      const rawValue = parseInt(parts[1].trim(), 10);

      if (!Number.isNaN(mappedValue) && !Number.isNaN(rawValue)) {
        channel.mappedValue = mappedValue;
        channel.rawValue = rawValue;
      }
      return;
    }

    const rawValue = parseInt(channelString, 10);
    if (!Number.isNaN(rawValue)) {
      channel.rawValue = rawValue;
      if (rawValue >= 800 && rawValue <= 2200) {
        channel.mappedValue = rawValue;
      }
    }
  });

  trackAutoCalibration();
}

function createAutoCalibrationSeed() {
  return Object.fromEntries(
    channels.value.slice(0, 4).map(channel => [
      channel.id,
      {
        min: channel.rawValue,
        max: channel.rawValue,
        current: channel.rawValue,
      },
    ])
  );
}

function startAutoCalibration() {
  autoCalibrationDraft.value = createAutoCalibrationSeed();
  autoCalibrationState.value = 'capturing';
}

function cancelAutoCalibration() {
  autoCalibrationDraft.value = {};
  autoCalibrationState.value = 'idle';
}

function trackAutoCalibration() {
  if (autoCalibrationState.value !== 'capturing') {
    return;
  }

  const nextDraft = { ...autoCalibrationDraft.value };
  channels.value.slice(0, 4).forEach(channel => {
    const existing = nextDraft[channel.id] ?? {
      min: channel.rawValue,
      max: channel.rawValue,
      current: channel.rawValue,
    };

    nextDraft[channel.id] = {
      min: Math.min(existing.min, channel.rawValue),
      max: Math.max(existing.max, channel.rawValue),
      current: channel.rawValue,
    };
  });

  autoCalibrationDraft.value = nextDraft;
}

function finishAutoCalibration() {
  if (autoCalibrationState.value !== 'capturing') {
    return;
  }

  channels.value.slice(0, 4).forEach(channel => {
    const draft = autoCalibrationDraft.value[channel.id];
    if (!draft) {
      return;
    }

    const nextMin = Math.min(draft.min, draft.max);
    const nextMax = Math.max(draft.min, draft.max);
    const nextMid = Math.min(Math.max(channel.rawValue, nextMin), nextMax);

    channel.cal.min = nextMin;
    channel.cal.mid = nextMid;
    channel.cal.max = nextMax;
  });

  autoCalibrationState.value = 'idle';
  window.alert('自动校准结果已写入 CH1-CH4，请点“保存校准”同步到设备。');
}

function applyCrsfSnapshot(snapshot) {
  let receivedMenuPayload = false;

  if (snapshot.status) {
    crsfStatus.value = {
      ...crsfStatus.value,
      isReady: snapshot.status.isReady ?? snapshot.status.is_ready ?? crsfStatus.value.isReady,
      isLinked: snapshot.status.isLinked ?? snapshot.status.is_linked ?? crsfStatus.value.isLinked,
      rssi: snapshot.status.rssi ?? crsfStatus.value.rssi,
      lq: snapshot.status.lq ?? crsfStatus.value.lq,
      loadedParams: snapshot.status.loadedParams ?? snapshot.status.loaded_params ?? crsfStatus.value.loadedParams,
      totalParams: snapshot.status.totalParams ?? snapshot.status.total_params ?? crsfStatus.value.totalParams,
      deviceLabel: snapshot.status.deviceLabel ?? snapshot.status.device_label ?? 'ESP32 live data',
    };
  }

  if (Array.isArray(snapshot.menus)) {
    crsfMenus.value = snapshot.menus
      .map(normalizeCrsfMenuItem)
      .filter(item => Number.isFinite(item.id) && item.id > 0);
    receivedMenuPayload = true;
  }

  hasLiveCrsfData.value = true;
  if (receivedMenuPayload) {
    crsfLoading.value = false;
    return;
  }

  if (snapshot.status) {
    const loadedParams = Number(snapshot.status.loadedParams ?? snapshot.status.loaded_params ?? -1);
    const totalParams = Number(snapshot.status.totalParams ?? snapshot.status.total_params ?? -1);
    if (totalParams > 0 && loadedParams >= totalParams) {
      crsfLoading.value = false;
    }
  }
}

function tryApplyCrsfPacket(line) {
  try {
    if (line.startsWith('{') && line.endsWith('}')) {
      const payload = JSON.parse(line);
      if (payload.type === 'crsf_snapshot') {
        applyCrsfSnapshot(payload);
        return true;
      }
      if (payload.type === 'crsf_status') {
        applyCrsfSnapshot({ status: payload.status ?? payload });
        return true;
      }
      if (payload.type === 'crsf_menu') {
        applyCrsfSnapshot({ menus: payload.menus ?? [] });
        return true;
      }
    }

    if (line.startsWith('CRSF_SNAPSHOT:')) {
      applyCrsfSnapshot(JSON.parse(line.slice('CRSF_SNAPSHOT:'.length)));
      return true;
    }
    if (line.startsWith('CRSF_STATUS:')) {
      applyCrsfSnapshot({ status: JSON.parse(line.slice('CRSF_STATUS:'.length)) });
      return true;
    }
    if (line.startsWith('CRSF_MENU:')) {
      applyCrsfSnapshot({ menus: JSON.parse(line.slice('CRSF_MENU:'.length)) });
      return true;
    }
  } catch (error) {
    console.warn('Failed to parse CRSF payload:', error, line);
  }

  return false;
}

function handleWebSocketData(data) {
  incomingDataBuffer.value += data;

  const lines = incomingDataBuffer.value.split(/\r?\n/);
  incomingDataBuffer.value = lines.pop() ?? '';

  lines
    .map(line => line.trim())
    .filter(Boolean)
    .forEach(line => {
      if (tryApplyCrsfPacket(line)) {
        return;
      }

      if (line.startsWith('C:')) {
        applyCalibrationLine(line);
        return;
      }

      if (applyModeLine(line)) {
        return;
      }

      applyRealtimeLine(line);
    });
}

function handleSocketStatus(connected) {
  isSocketConnected.value = connected;
}

function sendBridgeMessage(payload) {
  if (!wsConnection.value) {
    return false;
  }

  if (typeof payload === 'string') {
    wsConnection.value.sendData(payload);
    return true;
  }

  wsConnection.value.sendData(JSON.stringify(payload));
  return true;
}

function clearCrsfLoadingSoon() {
  window.setTimeout(() => {
    crsfLoading.value = false;
  }, 600);
}

function handleCrsfRefresh() {
  crsfLoading.value = true;
  sendBridgeMessage('CRSF_REFRESH');
}

function handleCrsfBind(item) {
  crsfLoading.value = true;
  sendBridgeMessage(`CRSF_BIND:${item.id}`);
  clearCrsfLoadingSoon();
}

function handleCrsfCommand(item) {
  crsfLoading.value = true;
  sendBridgeMessage(`CRSF_COMMAND:${item.id}`);
  clearCrsfLoadingSoon();
}

function handleCrsfSelectChange(payload) {
  crsfMenus.value = crsfMenus.value.map(item =>
    item.id === payload.id ? { ...item, value: payload.value } : item
  );

  sendBridgeMessage(`CRSF_WRITE:${payload.id}:${payload.value}`);
}

function requestCalibration() {
  if (wsConnection.value) {
    wsConnection.value.sendData('GET_CAL');
  }
}

function handleSocketConnected() {
  requestCalibration();
}

function sendCalibrationData() {
  if (!wsConnection.value) {
    window.alert('未连接到设备，无法保存。');
    return;
  }

  wsConnection.value.sendData(`M:${simMode.value}`);

  const calibrationString = channels.value
    .map(channel => `${channel.id},${channel.cal.min},${channel.cal.mid},${channel.cal.max}`)
    .join(';');

  wsConnection.value.sendData(`C:${calibrationString}`);
  window.alert('模式与校准数据已发送。');
}

function setSimMode(nextMode) {
  simMode.value = nextMode;
}

const currentPage = computed(() => pages.find(page => page.id === activePage.value) ?? pages[0]);
const isXboxMode = computed(() => simMode.value === SIM_MODE_XBOX);

const leftStick = computed(() => ({
  x: (channels.value[3].mappedValue - 1500) / 5,
  y: (channels.value[2].mappedValue - 1500) / 5,
}));

const rightStick = computed(() => ({
  x: (channels.value[0].mappedValue - 1500) / 5,
  y: (channels.value[1].mappedValue - 1500) / 5,
}));

const primaryChannels = computed(() => channels.value.slice(0, 4));

const channelExtremes = computed(() => {
  const mappedValues = channels.value.map(channel => channel.mappedValue);
  return {
    min: Math.min(...mappedValues),
    max: Math.max(...mappedValues),
  };
});

const liveChannelCount = computed(
  () => channels.value.filter(channel => channel.mappedValue !== 1500 || channel.rawValue !== 2048).length
);
const autoCalibrationChannels = computed(() =>
  channels.value.slice(0, 4).map(channel => ({
    id: channel.id,
    label: `CH${channel.id}`,
    rawValue: channel.rawValue,
    min: autoCalibrationDraft.value[channel.id]?.min ?? channel.rawValue,
    max: autoCalibrationDraft.value[channel.id]?.max ?? channel.rawValue,
  }))
);
const isAutoCalibrationActive = computed(() => autoCalibrationState.value === 'capturing');

const crsfPanelDescription = computed(() => {
  return '';
});

const calibrationHeadline = computed(() => {
  return `${liveChannelCount.value}/16 通道已在活动`;
});

const pageStatusCards = computed(() => [
  {
    label: '连接',
    value: isSocketConnected.value ? '在线' : '离线',
    accent: isSocketConnected.value,
  },
  {
    label: '模式',
    value: isXboxMode.value ? 'XBOX' : 'HID',
    accent: true,
  },
  {
    label: 'CRSF',
    value: `${crsfStatus.value.loadedParams ?? 0}/${crsfStatus.value.totalParams ?? 0}`,
    accent: Boolean(crsfStatus.value.isReady),
  },
]);
</script>

<template>
  <div class="app-shell">
    <aside class="app-sidebar">
      <nav class="sidebar-nav" aria-label="Primary">
        <button
          v-for="page in pages"
          :key="page.id"
          type="button"
          class="nav-link"
          :class="{ active: activePage === page.id }"
          @click="activePage = page.id"
        >
          <span class="nav-label">{{ page.label }}</span>
        </button>
      </nav>

      <section class="sidebar-summary">
        <article
          v-for="card in pageStatusCards"
          :key="card.label"
          class="summary-card"
          :class="{ accent: card.accent }"
        >
          <span>{{ card.label }}</span>
          <strong>{{ card.value }}</strong>
        </article>
      </section>

      <div class="sidebar-actions">
        <button type="button" class="primary-action" @click="sendCalibrationData">
          保存校准
        </button>
        <button type="button" class="secondary-action" @click="handleCrsfRefresh">
          刷新 CRSF
        </button>
      </div>
    </aside>

    <main class="app-main">
      <section v-if="activePage === 'controls'" class="controls-page">
        <section class="hero-card">
          <div class="hero-actions controls-toolbar">
            <div class="mode-switch-group toolbar-block">
              <button
                type="button"
                class="mode-switch-button"
                :class="{ active: !isXboxMode }"
                @click="setSimMode(SIM_MODE_DEFAULT)"
              >
                HID
              </button>
              <button
                type="button"
                class="mode-switch-button"
                :class="{ active: isXboxMode }"
                @click="setSimMode(SIM_MODE_XBOX)"
              >
                XBOX
              </button>
            </div>

            <div class="hero-metrics">
              <article class="metric-card">
                <span>实时状态</span>
                <strong>{{ calibrationHeadline }}</strong>
                <small>{{ channelExtremes.min }} ~ {{ channelExtremes.max }}</small>
              </article>
              <article class="metric-card">
                <span>链路快照</span>
                <strong>{{ crsfStatus.isLinked ? '已链路' : '未链路' }}</strong>
                <small>RSSI {{ crsfStatus.rssi ?? '--' }} · LQ {{ crsfStatus.lq ?? '--' }}</small>
              </article>
            </div>
          </div>
        </section>

        <section class="joystick-section">
          <article class="panel-card sticks-card">
            <div class="panel-head">
              <div>
                <p class="panel-kicker">Stick View</p>
                <h3>双摇杆预览</h3>
              </div>
              <small>CH1-CH4</small>
            </div>

            <div class="stick-grid">
              <Joystick :x="leftStick.x" :y="leftStick.y" label="左摇杆" />
              <Joystick :x="rightStick.x" :y="rightStick.y" label="右摇杆" />
            </div>
          </article>

          <article class="panel-card primary-card">
            <div class="panel-head">
              <div>
                <p class="panel-kicker">Primary Channels</p>
                <h3>主通道概览</h3>
              </div>
              <small>Roll / Pitch / Throttle / Yaw</small>
            </div>

            <div class="primary-grid">
              <div v-for="channel in primaryChannels" :key="channel.id" class="primary-tile">
                <span>CH{{ channel.id }}</span>
                <strong>{{ channel.mappedValue }}</strong>
                <small>RAW {{ channel.rawValue }}</small>
              </div>
            </div>
          </article>
        </section>

        <section class="panel-card calibration-panel">
          <div class="panel-head">
            <div>
              <p class="panel-kicker">Channel Calibration</p>
              <h3>16 通道校准面板</h3>
            </div>
          </div>

          <section class="auto-calibration-card">
            <div class="auto-calibration-copy">
              <strong>主通道自动校准</strong>
            </div>

            <div class="auto-calibration-actions">
              <button
                v-if="!isAutoCalibrationActive"
                type="button"
                class="secondary-action compact-action"
                @click="startAutoCalibration"
              >
                开始自动校准
              </button>
              <button
                v-else
                type="button"
                class="primary-action compact-action"
                @click="finishAutoCalibration"
              >
                回中完成并写入
              </button>
              <button
                v-if="isAutoCalibrationActive"
                type="button"
                class="secondary-action compact-action"
                @click="cancelAutoCalibration"
              >
                取消
              </button>
            </div>

            <div class="auto-calibration-grid">
              <article
                v-for="channel in autoCalibrationChannels"
                :key="channel.id"
                class="auto-calibration-tile"
              >
                <span>{{ channel.label }}</span>
                <strong>{{ channel.rawValue }}</strong>
                <small>Min {{ channel.min }} · Max {{ channel.max }}</small>
              </article>
            </div>
          </section>

          <div class="channel-grid">
            <AuxChannelSlider
              v-for="channel in channels"
              :key="channel.id"
              :channel-id="channel.id"
              :mapped-value="channel.mappedValue"
              v-model:raw-value="channel.rawValue"
              :cal="channel.cal"
            />
          </div>
        </section>
      </section>

      <section v-else class="crsf-page">
        <CrsfConfiguratorPanel
          :menus="crsfMenus"
          :status="crsfStatus"
          :loading="crsfLoading"
          :description="crsfPanelDescription"
          @refresh="handleCrsfRefresh"
          @bind="handleCrsfBind"
          @command="handleCrsfCommand"
          @select-change="handleCrsfSelectChange"
        />
      </section>
    </main>

    <WebSocketConnection
      ref="wsConnection"
      @data="handleWebSocketData"
      @status="handleSocketStatus"
      @connected="handleSocketConnected"
    />
  </div>
</template>

<style>
:root {
  --app-bg: #efe8dc;
  --app-ink: #1e2628;
  --app-muted: #6c7673;
  --app-border: rgba(30, 38, 40, 0.1);
  --app-panel: rgba(255, 252, 247, 0.9);
  --app-panel-strong: #fffdf8;
  --app-accent: #0d7f77;
  --app-accent-strong: #0a6660;
  --app-accent-soft: rgba(13, 127, 119, 0.12);
  --app-warm: #c57a34;
  --text-color: var(--app-ink);
  --text-secondary-color: var(--app-muted);
  --border-color: var(--app-border);
  --accent-color: var(--app-accent);
  --glow-color: rgba(13, 127, 119, 0.18);
  --panel-bg-color: var(--app-panel);
  --channel-card-bg: rgba(255, 251, 245, 0.92);
  --channel-card-border: rgba(30, 38, 40, 0.08);
  --input-bg: #f7f1e8;
  --joystick-surface:
    radial-gradient(circle at 30% 30%, rgba(13, 127, 119, 0.14), transparent 40%),
    linear-gradient(180deg, rgba(255, 255, 255, 0.94), rgba(242, 235, 224, 0.98));
}

html,
body {
  margin: 0;
  min-height: 100%;
  background:
    radial-gradient(circle at top left, rgba(13, 127, 119, 0.16), transparent 28%),
    radial-gradient(circle at right center, rgba(197, 122, 52, 0.1), transparent 24%),
    linear-gradient(180deg, #faf6ef 0%, var(--app-bg) 100%);
  color: var(--app-ink);
  font-family: 'Avenir Next', 'Trebuchet MS', 'PingFang SC', sans-serif;
}

body {
  overflow-x: hidden;
}

#app {
  width: 100vw;
  min-height: 100dvh;
  max-width: none;
  margin: 0;
  padding: 0;
}

button,
input,
textarea,
select {
  font: inherit;
}

.app-shell {
  width: 100%;
  min-height: 100dvh;
  display: grid;
  grid-template-columns: 280px minmax(0, 1fr);
}

.app-sidebar {
  position: sticky;
  top: 0;
  display: grid;
  align-content: start;
  gap: 24px;
  min-height: 100dvh;
  padding: 28px 22px;
  box-sizing: border-box;
  border-right: 1px solid rgba(30, 38, 40, 0.08);
  background:
    linear-gradient(180deg, rgba(255, 252, 248, 0.94), rgba(245, 237, 226, 0.96));
  backdrop-filter: blur(14px);
}

.hero-card h3,
.panel-head h3 {
  margin: 0;
  font-family: 'Georgia', 'Times New Roman', serif;
  font-weight: 700;
  letter-spacing: 0.01em;
}

.sidebar-kicker,
.hero-kicker,
.panel-kicker {
  margin: 0 0 8px;
  color: var(--app-accent);
  font-size: 11px;
  font-weight: 700;
  letter-spacing: 0.18em;
  text-transform: uppercase;
}

.hero-copy p,
.summary-card small,
.metric-card small {
  color: var(--app-muted);
  line-height: 1.65;
}

.sidebar-nav {
  display: grid;
  gap: 10px;
  margin-top: 8px;
}

.nav-link {
  display: grid;
  place-items: center;
  gap: 0;
  padding: 14px 16px;
  text-align: center;
  border: 1px solid rgba(30, 38, 40, 0.08);
  border-radius: 18px;
  background: rgba(255, 251, 245, 0.8);
  color: var(--app-ink);
  cursor: pointer;
  transition: transform 0.2s ease, border-color 0.2s ease, box-shadow 0.2s ease;
}

.nav-link:hover {
  transform: translateY(-1px);
  border-color: rgba(13, 127, 119, 0.22);
}

.nav-link.active {
  border-color: rgba(13, 127, 119, 0.28);
  background: linear-gradient(180deg, rgba(13, 127, 119, 0.12), rgba(255, 251, 245, 0.96));
  box-shadow: 0 18px 32px rgba(13, 127, 119, 0.12);
}

.nav-label {
  font-size: 1rem;
  font-weight: 700;
}

.sidebar-summary,
.sidebar-actions {
  display: grid;
  gap: 12px;
}

.summary-card {
  display: grid;
  gap: 2px;
  padding: 14px 16px;
  border: 1px solid rgba(30, 38, 40, 0.08);
  border-radius: 18px;
  background: rgba(255, 253, 248, 0.78);
}

.summary-card span {
  color: var(--app-muted);
  font-size: 0.82rem;
  text-transform: uppercase;
  letter-spacing: 0.08em;
}

.summary-card strong {
  font-size: 1.05rem;
}

.summary-card.accent {
  border-color: rgba(13, 127, 119, 0.2);
  box-shadow: inset 0 0 0 1px rgba(13, 127, 119, 0.05);
}

.primary-action,
.secondary-action {
  width: 100%;
  padding: 13px 16px;
  border-radius: 999px;
  cursor: pointer;
  transition: transform 0.2s ease, box-shadow 0.2s ease, background 0.2s ease;
}

.primary-action {
  border: none;
  background: linear-gradient(135deg, var(--app-accent), var(--app-accent-strong));
  color: #f7fffe;
  box-shadow: 0 18px 36px rgba(13, 127, 119, 0.22);
}

.secondary-action {
  border: 1px solid rgba(30, 38, 40, 0.1);
  background: rgba(255, 251, 245, 0.85);
  color: var(--app-ink);
}

.primary-action:hover,
.secondary-action:hover,
.mode-switch-button:hover {
  transform: translateY(-1px);
}

.app-main {
  display: grid;
  align-content: start;
  gap: 24px;
  padding: 28px;
  box-sizing: border-box;
  min-width: 0;
}

.topbar,
.hero-card,
.panel-card {
  border: 1px solid var(--app-border);
  border-radius: 28px;
  background: var(--app-panel);
  box-shadow: 0 24px 60px rgba(49, 58, 63, 0.08);
}

.controls-page,
.crsf-page {
  display: grid;
  gap: 22px;
}

.hero-card {
  display: block;
  padding: 18px 20px;
  background:
    radial-gradient(circle at top left, rgba(13, 127, 119, 0.14), transparent 34%),
    radial-gradient(circle at bottom right, rgba(197, 122, 52, 0.12), transparent 28%),
    linear-gradient(180deg, rgba(255, 255, 255, 0.94), rgba(246, 239, 228, 0.94));
}

.hero-actions,
.hero-metrics {
  display: grid;
  gap: 14px;
  align-content: start;
}

.hero-actions {
  min-width: 0;
}

.controls-toolbar {
  display: flex;
  align-items: stretch;
  gap: 14px;
  flex-wrap: nowrap;
}

.mode-switch-group {
  display: inline-flex;
  padding: 4px;
  border: 1px solid rgba(13, 127, 119, 0.14);
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.72);
}

.mode-switch-button {
  min-width: 92px;
  padding: 11px 16px;
  border: none;
  border-radius: 999px;
  background: transparent;
  color: var(--app-muted);
  cursor: pointer;
  font-weight: 700;
}

.mode-switch-button.active {
  background: linear-gradient(135deg, var(--app-accent), var(--app-accent-strong));
  color: #faffff;
  box-shadow: 0 12px 26px rgba(13, 127, 119, 0.22);
}

.metric-card {
  display: grid;
  gap: 6px;
  min-width: 0;
  padding: 14px 18px;
  border: 1px solid rgba(30, 38, 40, 0.08);
  border-radius: 20px;
  background: rgba(255, 253, 248, 0.82);
}

.metric-card small {
  line-height: 1.3;
}

.metric-card span {
  color: var(--app-muted);
  font-size: 0.82rem;
  text-transform: uppercase;
  letter-spacing: 0.08em;
}

.metric-card strong {
  font-size: 1.15rem;
}

.hero-metrics {
  flex: 1 1 auto;
  grid-template-columns: repeat(2, minmax(0, 1fr));
}

.joystick-section {
  display: grid;
  grid-template-columns: 1.05fr 0.95fr;
  gap: 22px;
}

.panel-card {
  padding: 24px;
}

.panel-head {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  align-items: flex-start;
  margin-bottom: 20px;
}

.panel-head small {
  color: var(--app-muted);
}

.stick-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 20px;
  justify-items: center;
}

.primary-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 14px;
}

.primary-tile {
  display: grid;
  gap: 6px;
  padding: 16px;
  border: 1px solid rgba(30, 38, 40, 0.08);
  border-radius: 20px;
  background: linear-gradient(180deg, rgba(255, 255, 255, 0.78), rgba(246, 239, 228, 0.84));
}

.primary-tile span,
.primary-tile small {
  color: var(--app-muted);
}

.primary-tile strong {
  font-size: 1.4rem;
}

.channel-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 14px;
}

.auto-calibration-card {
  display: grid;
  gap: 14px;
  margin-bottom: 18px;
  padding: 18px;
  border: 1px solid rgba(13, 127, 119, 0.14);
  border-radius: 22px;
  background:
    radial-gradient(circle at top right, rgba(13, 127, 119, 0.08), transparent 28%),
    linear-gradient(180deg, rgba(255, 255, 255, 0.94), rgba(246, 239, 228, 0.92));
}

.auto-calibration-copy {
  display: grid;
  gap: 0;
}

.auto-calibration-copy strong {
  font-size: 1.05rem;
}

.auto-calibration-actions {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
}

.compact-action {
  width: auto;
  min-width: 160px;
  padding: 11px 18px;
}

.auto-calibration-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 12px;
}

.auto-calibration-tile {
  display: grid;
  gap: 4px;
  padding: 14px;
  border: 1px solid rgba(30, 38, 40, 0.08);
  border-radius: 18px;
  background: rgba(255, 253, 248, 0.88);
}

.auto-calibration-tile span,
.auto-calibration-tile small {
  color: var(--app-muted);
}

.auto-calibration-tile strong {
  font-size: 1.2rem;
}

@media (max-width: 1180px) {
  .app-shell {
    grid-template-columns: 1fr;
  }

  .app-sidebar {
    position: static;
    min-height: auto;
    border-right: none;
    border-bottom: 1px solid rgba(30, 38, 40, 0.08);
  }

  .sidebar-nav,
  .sidebar-summary,
  .sidebar-actions {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }

  .controls-toolbar {
    flex-wrap: wrap;
  }

  .hero-metrics {
    width: 100%;
  }

  .joystick-section,
  .channel-grid {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 780px) {
  .app-main,
  .app-sidebar {
    padding: 18px;
  }

  .hero-card,
  .panel-card {
    border-radius: 22px;
  }

  .hero-card,
  .panel-head {
    display: grid;
  }

  .sidebar-nav,
  .channel-grid {
    grid-template-columns: 1fr;
  }

  .sidebar-nav {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }

  .sidebar-summary {
    grid-template-columns: repeat(3, minmax(0, 1fr));
    gap: 10px;
  }

  .summary-card {
    gap: 4px;
    padding: 10px 8px;
    border-radius: 16px;
    text-align: center;
  }

  .summary-card span {
    font-size: 0.68rem;
    letter-spacing: 0.04em;
  }

  .summary-card strong {
    font-size: 1rem;
    line-height: 1.1;
  }

  .sidebar-actions {
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 10px;
  }

  .primary-action,
  .secondary-action {
    padding: 12px 10px;
    font-size: 0.96rem;
    font-weight: 700;
  }

  .controls-toolbar,
  .hero-metrics {
    grid-template-columns: 1fr;
    width: 100%;
  }

  .controls-toolbar {
    display: grid;
    justify-items: center;
  }

  .toolbar-block {
    width: 100%;
    display: flex;
    justify-content: center;
  }

  .hero-actions,
  .hero-metrics {
    min-width: 0;
  }

  .mode-switch-group {
    justify-self: center;
  }

  .stick-grid,
  .primary-grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 12px;
  }

  .panel-head {
    gap: 10px;
    margin-bottom: 16px;
    width: 100%;
    justify-items: center;
    justify-content: center;
    align-items: center;
    text-align: center;
  }

  .panel-head > div {
    display: grid;
    justify-items: center;
    align-items: center;
    width: 100%;
    margin: 0 auto;
  }

  .panel-head small {
    display: block;
    width: 100%;
    margin: 0 auto;
    text-align: center;
  }
}

@media (max-width: 520px) {
  .app-main,
  .app-sidebar {
    padding: 14px;
  }

  .nav-link {
    padding: 13px 12px;
    border-radius: 16px;
  }

  .nav-label {
    font-size: 0.96rem;
  }

  .auto-calibration-card {
    padding: 14px;
    border-radius: 18px;
  }

  .auto-calibration-grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }

  .compact-action {
    width: 100%;
    min-width: 0;
  }
}
</style>
