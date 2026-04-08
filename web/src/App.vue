<script setup>
import { ref, onMounted, onUnmounted, onUpdated, computed } from 'vue';
import WebSocketConnection from './components/WebSocketConnection.vue';
import Joystick from './components/Joystick.vue';
import AuxChannelSlider from './components/AuxChannelSlider.vue';

const wsConnection = ref(null);
const incomingDataBuffer = ref('');
const sidebarScroller = ref(null);
const showSidebarScrollbar = ref(false);
const sidebarThumbHeight = ref(0);
const sidebarThumbOffset = ref(0);
let sidebarScrollbarFrame = 0;
const SIM_MODE_DEFAULT = 0;
const SIM_MODE_XBOX = 1;
const simMode = ref(SIM_MODE_DEFAULT);

// 【修改1】：将 cal (校准数据) 放到全局 channels 状态中管理，保证绝对的响应式
const channels = ref(Array.from({ length: 16 }, (_, i) => ({
  id: i + 1,
  mappedValue: 1500, // 新增：用来存 ESP32 发来的 1000~2000 映射值
  rawValue: 2048,    // ESP32 发来的原始物理值
  cal: { min: 1000, mid: 1500, max: 2000 } // 初始校准值
})));

// --- FAKE DATA SIMULATION ---
let dataSimulator = null;
onMounted(() => {
  // dataSimulator = setInterval(() => {
  //   channels.value.forEach(ch => {
  //     const movement = (Math.random() - 0.5) * 50;
  //     let newValue = ch.rawValue + movement;
  //     if (newValue < 1000) newValue = 1000;
  //     if (newValue > 2000) newValue = 2000;
  //     ch.rawValue = Math.round(newValue);
  //   });
  // }, 100);
  window.addEventListener('resize', scheduleSidebarScrollbarUpdate);
  scheduleSidebarScrollbarUpdate();
});
onUnmounted(() => {
  if (dataSimulator) clearInterval(dataSimulator);
  if (sidebarScrollbarFrame) {
    cancelAnimationFrame(sidebarScrollbarFrame);
  }
  window.removeEventListener('resize', scheduleSidebarScrollbarUpdate);
});
onUpdated(() => {
  scheduleSidebarScrollbarUpdate();
});
// --- END FAKE DATA ---

const sidebarThumbStyle = computed(() => ({
  height: `${sidebarThumbHeight.value}px`,
  transform: `translateY(${sidebarThumbOffset.value}px)`,
}));
const isXboxMode = computed(() => simMode.value === SIM_MODE_XBOX);

function updateSidebarScrollbar() {
  const element = sidebarScroller.value;
  if (!element) {
    return;
  }

  const hasOverflow = element.scrollHeight > element.clientHeight + 1;
  showSidebarScrollbar.value = hasOverflow;

  if (!hasOverflow) {
    sidebarThumbHeight.value = 0;
    sidebarThumbOffset.value = 0;
    return;
  }

  const thumbHeight = Math.max(
    (element.clientHeight / element.scrollHeight) * element.clientHeight,
    36,
  );
  const maxScrollTop = element.scrollHeight - element.clientHeight;
  const maxThumbOffset = element.clientHeight - thumbHeight;
  const thumbOffset =
    maxScrollTop > 0 ? (element.scrollTop / maxScrollTop) * maxThumbOffset : 0;

  sidebarThumbHeight.value = thumbHeight;
  sidebarThumbOffset.value = thumbOffset;
}

function scheduleSidebarScrollbarUpdate() {
  if (sidebarScrollbarFrame) {
    cancelAnimationFrame(sidebarScrollbarFrame);
  }

  sidebarScrollbarFrame = requestAnimationFrame(() => {
    sidebarScrollbarFrame = 0;
    updateSidebarScrollbar();
  });
}

function applyCalibrationLine(line) {
  const entries = line.slice(2).split(';');
  entries.forEach(entry => {
    const [id, min, mid, max] = entry.split(',').map(v => parseInt(v.trim(), 10));
    const ch = channels.value.find(c => c.id === id);
    if (ch && !isNaN(min) && !isNaN(mid) && !isNaN(max)) {
      ch.cal.min = min;
      ch.cal.mid = mid;
      ch.cal.max = max;
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

  channelStrings.forEach((chStr, index) => {
    const channel = channels.value[index];
    if (!channel) {
      return;
    }

    const parts = chStr.split(':');
    if (parts.length === 2) {
      const mappedVal = parseInt(parts[0].trim(), 10);
      const rawVal = parseInt(parts[1].trim(), 10);

      if (!isNaN(mappedVal) && !isNaN(rawVal)) {
        channel.mappedValue = mappedVal;
        channel.rawValue = rawVal;
      }
      return;
    }

    const rawVal = parseInt(chStr, 10);
    if (!isNaN(rawVal)) {
      channel.rawValue = rawVal;

      // 兼容后 12 个通道只上报单值的情况。
      if (rawVal >= 800 && rawVal <= 2200) {
        channel.mappedValue = rawVal;
      }
    }
  });
}

function handleWebSocketData(data) {
  if (dataSimulator) {
    clearInterval(dataSimulator);
    dataSimulator = null;
  }
  incomingDataBuffer.value += data;

  const lines = incomingDataBuffer.value.split(/\r?\n/);
  incomingDataBuffer.value = lines.pop() ?? '';

  lines
    .map(line => line.trim())
    .filter(Boolean)
    .forEach(line => {
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

function sendCalibrationData() {
  if (!wsConnection.value) {
    alert("未连接到设备，无法保存！");
    return;
  }

  wsConnection.value.sendData(`M:${simMode.value}`);

  const calibrationString = channels.value
    .map(ch => `${ch.id},${ch.cal.min},${ch.cal.mid},${ch.cal.max}`)
    .join(';');
  const command = `C:${calibrationString}`;
  wsConnection.value.sendData(command);
  alert(`已发送模式和校准数据`);
}

function requestCalibration() {
  if (wsConnection.value) {
    wsConnection.value.sendData('GET_CAL');
  }
}

function setSimMode(nextMode) {
  simMode.value = nextMode;
}

// 摇杆控制计算：直接用 ESP32 发过来的 1000~2000 的 mappedValue 转成 -100~100 的百分比
const rightStick = computed(() => {
  return { 
    x: (channels.value[0].mappedValue - 1500) / 5, // CH1: 横滚 Roll (左右)
    y: (channels.value[1].mappedValue - 1500) / 5  // CH2: 俯仰 Pitch (上下)
  };
});

const leftStick = computed(() => {
  return { 
    x: (channels.value[3].mappedValue - 1500) / 5, // CH4: 偏航 Yaw (左右)
    y: (channels.value[2].mappedValue - 1500) / 5  // CH3: 油门 Throttle (上下)
  };
});

</script>

<template>
  <div id="app-grid">
    <header class="app-header">
      <div class="mode-switch-panel">
        <div class="mode-switch-group">
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
      </div>
      <a
        class="brand-title"
        href="https://darwinfpv.com/"
        target="_blank"
        rel="noopener noreferrer"
      >
        DARWIN
      </a>
      <button @click="sendCalibrationData" class="save-button">保存</button>
    </header>

    <aside class="app-sidebar">
      <h4>通道校准</h4>
      <div class="sidebar-scroll-shell">
        <div
          ref="sidebarScroller"
          class="sidebar-content-wrapper"
          @scroll="updateSidebarScrollbar"
          @click.capture="scheduleSidebarScrollbarUpdate"
        >
          <AuxChannelSlider
            v-for="ch in channels"
            :key="ch.id"
            :channel-id="ch.id"
            :mapped-value="ch.mappedValue"
            v-model:raw-value="ch.rawValue"
            :cal="ch.cal" 
          />
        </div>
        <div v-if="showSidebarScrollbar" class="sidebar-scrollbar" aria-hidden="true">
          <div class="sidebar-scrollbar-thumb" :style="sidebarThumbStyle"></div>
        </div>
      </div>
    </aside>

    <footer class="app-footer">
      <Joystick :x="leftStick.x" :y="leftStick.y" label="左摇杆" />
      <Joystick :x="rightStick.x" :y="rightStick.y" label="右摇杆" />
    </footer>

    <WebSocketConnection 
      ref="wsConnection" 
      @data="handleWebSocketData"
      @connected="requestCalibration"
    />
  </div>
</template>

<style>
/* * ==================================
  * FORCEFUL GLOBAL & APP OVERRIDES
  * ==================================
*/
:root {
  --bg-color: #1e1e1e;
  --panel-bg-color: #242424;
  --border-color: #333;
  --text-color: #f0f0f5;
  --text-secondary-color: #a0a0a5;
  --header-height: 60px;
  --accent-color: #00bfff;
  --glow-color: rgba(0, 191, 255, 0.5);
}

html, body {
  margin: 0 !important;
  padding: 0 !important;
  width: 100vw !important;
  height: 100dvh !important;
  overflow: hidden !important;
  background-color: var(--bg-color);
  color: var(--text-color);
  font-family: 'Segoe UI', 'Roboto', 'Oxygen', 'Ubuntu', sans-serif;
}

#app {
  max-width: none !important;
  width: 100vw !important;
  height: 100dvh !important;
  margin: 0 !important;
  padding: 0 !important;
  border: none !important;
}

#app-grid {
  display: grid;
  width: 100%;
  height: 100dvh;
  box-sizing: border-box;
  grid-template-columns: 1fr 400px;
  grid-template-rows: var(--header-height) minmax(0, 1fr);
  grid-template-areas:
    "header sidebar"
    "joysticks sidebar";
  overflow: hidden;
}

.app-header { 
  grid-area: header; 
  display: grid;
  grid-template-columns: 1fr auto 1fr;
  align-items: center; 
  gap: 1rem;
  padding: 0 2rem;
  border-bottom: 1px solid rgba(255, 255, 255, 0.04);
}

.brand-title {
  grid-column: 2;
  justify-self: center;
  display: inline-flex;
  align-items: center;
  font-size: 1.15rem;
  font-weight: 700;
  letter-spacing: 0.22rem;
  color: var(--text-color);
  text-transform: uppercase;
  white-space: nowrap;
  text-decoration: none;
  transition: color 0.25s ease, text-shadow 0.25s ease;
}

.brand-title:hover {
  color: var(--accent-color);
  text-shadow: 0 0 12px rgba(0, 191, 255, 0.28);
}

.mode-switch-panel {
  justify-self: start;
  display: flex;
  align-items: center;
  gap: 0.4rem;
  min-width: 0;
}

.mode-switch-group {
  display: inline-flex;
  padding: 0.2rem;
  border: 1px solid rgba(0, 191, 255, 0.28);
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.04);
  box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.02);
}

.mode-switch-button {
  min-width: 64px;
  padding: 0.45rem 0.9rem;
  border: none;
  border-radius: 999px;
  background: transparent;
  color: var(--text-secondary-color);
  font-size: 0.85rem;
  font-weight: 700;
  letter-spacing: 0.04rem;
  cursor: pointer;
  transition: all 0.25s ease;
}

.mode-switch-button.active {
  background: var(--accent-color);
  color: var(--bg-color);
  box-shadow: 0 0 12px rgba(0, 191, 255, 0.28);
}

.app-footer {
  grid-area: joysticks;
  display: flex;
  flex-wrap: wrap;
  justify-content: center;
  align-items: center;
  align-content: center;
  gap: 200px;
  padding: 1.5rem;
  min-height: 0;
  overflow: hidden;
  background-image: radial-gradient(circle, #2a2a2a, #1a1a1a);
}

.app-sidebar { 
  grid-area: sidebar; 
  background: var(--panel-bg-color); 
  padding: 1.5rem; 
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;
  overflow: hidden;
  box-sizing: border-box;
  border-left: 1px solid var(--border-color) !important;
}

h4 {
  text-align: center;
  margin-top: 0;
  margin-bottom: 1.5rem;
  color: var(--text-secondary-color);
  padding-bottom: 1rem;
  text-transform: uppercase;
  letter-spacing: 1px;
  flex-shrink: 0;
  border-bottom: 1px solid var(--border-color);
}

.sidebar-scroll-shell {
  position: relative;
  flex: 1;
  min-height: 0;
}

.sidebar-content-wrapper {
  height: 100%;
  flex-grow: 1;
  min-height: 0;
  overflow-y: scroll;
  display: flex;
  flex-direction: column;
  gap: 1rem;
  padding-right: 18px;
  scrollbar-gutter: stable;
  scrollbar-width: thin;
  scrollbar-color: var(--accent-color) transparent;
}
.sidebar-content-wrapper::-webkit-scrollbar { width: 8px; }
.sidebar-content-wrapper::-webkit-scrollbar-track { background: transparent; }
.sidebar-content-wrapper::-webkit-scrollbar-thumb {
  background-color: var(--accent-color);
  border-radius: 4px;
}

.sidebar-scrollbar {
  position: absolute;
  top: 0;
  right: 0;
  width: 8px;
  height: 100%;
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.08);
  pointer-events: none;
}

.sidebar-scrollbar-thumb {
  width: 100%;
  border-radius: 999px;
  background: linear-gradient(180deg, #61d9ff 0%, var(--accent-color) 100%);
  box-shadow: 0 0 10px rgba(0, 191, 255, 0.35);
}

.save-button {
  grid-column: 3;
  justify-self: end;
  background-color: transparent;
  color: var(--accent-color);
  border: 1px solid var(--accent-color);
  padding: 0.6rem 1.2rem;
  border-radius: 6px;
  font-size: 1rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.3s;
  box-shadow: 0 0 5px var(--glow-color);
}
.save-button:hover {
  background-color: var(--accent-color);
  color: var(--bg-color);
  box-shadow: 0 0 15px var(--glow-color);
}

@media (max-width: 1024px) {
  #app-grid {
    grid-template-columns: minmax(0, 1fr) 360px;
  }

  .app-footer {
    gap: 96px;
    padding: 1.25rem;
  }

  .app-sidebar {
    padding: 1.25rem;
  }
}

@media (max-width: 768px) {
  #app-grid {
    grid-template-columns: 1fr;
    grid-template-rows: auto auto minmax(0, 1fr);
    grid-template-areas:
      "header"
      "joysticks"
      "sidebar";
  }

  .app-header {
    position: sticky;
    top: 0;
    z-index: 10;
    padding: 0.9rem 1rem;
    background: rgba(30, 30, 30, 0.96);
    backdrop-filter: blur(10px);
  }

  .brand-title {
    font-size: 1rem;
    letter-spacing: 0.16rem;
  }

  .mode-switch-panel {
    gap: 0.3rem;
  }

  .mode-switch-group {
    padding: 0.16rem;
  }

  .mode-switch-button {
    min-width: 52px;
    padding: 0.38rem 0.62rem;
    font-size: 0.72rem;
  }

  .save-button {
    padding: 0.55rem 0.9rem;
    font-size: 0.95rem;
  }

  .app-footer {
    gap: 1.5rem;
    padding: 1.25rem 1rem 0.75rem;
    min-height: 220px;
  }

  .app-sidebar {
    border-left: none !important;
    border-top: 1px solid var(--border-color) !important;
    padding: 1rem;
    height: 100%;
  }

  .sidebar-content-wrapper {
    padding-right: 0.4rem;
    -webkit-overflow-scrolling: touch;
  }

  .sidebar-scrollbar {
    width: 6px;
  }

  h4 {
    margin-bottom: 1rem;
    padding-bottom: 0.75rem;
  }
}
</style>
