<template>
  <div class="grid grid-cols-1 lg:grid-cols-[280px_minmax(0,1fr)] w-full min-h-screen bg-black">
    <!-- Sidebar -->
    <aside class="relative lg:sticky top-0 grid content-start gap-5 lg:gap-6 h-auto lg:min-h-screen p-4 lg:p-7 box-border border-b lg:border-b-0 lg:border-r border-white/10 bg-gradient-to-b from-white/5 to-transparent backdrop-blur-md z-10">
      <!-- Navigation -->
      <nav class="grid grid-cols-2 lg:grid-cols-1 gap-2.5 mt-0 lg:mt-2">
        <button 
          v-for="nav in navItems" 
          :key="nav.id"
          type="button"
          @click="currentTab = nav.id"
          :class="['px-4 py-3.5 text-center border border-white/5 rounded-[18px] bg-darwin-panel/80 text-darwin-ink cursor-pointer transition-all duration-200 hover:-translate-y-px', { 'border-darwin-amber/50 bg-gradient-to-b from-darwin-amber/20 to-darwin-panel shadow-lg': currentTab === nav.id }]"
        >
          <span class="text-base font-bold">{{ nav.label }}</span>
        </button>
      </nav>

      <!-- Connection & Global Status -->
      <section class="grid grid-cols-3 lg:grid-cols-1 gap-3">
        <article 
          v-for="st in globalStatus" 
          :key="st.label"
          :class="['p-2.5 lg:p-3.5 border border-white/5 rounded-2xl bg-darwin-panel text-center lg:text-left', { 'border-darwin-amber/30': st.accent }]"
        >
          <span class="text-darwin-muted uppercase tracking-widest text-[0.68rem] lg:text-xs block mb-0.5">{{ st.label }}</span>
          <strong :class="['text-[1rem] lg:text-[1.05rem] leading-tight', { 'text-darwin-amber': st.accent }]">{{ st.value }}</strong>
        </article>
      </section>

      <!-- Language & Actions -->
      <div class="grid grid-cols-2 lg:grid-cols-1 gap-2.5 lg:gap-3 mt-auto">
        <select 
          :value="currentLang" 
          @change="changeLanguage"
          class="w-full px-2.5 py-3 font-bold border border-white/10 rounded-full bg-darwin-panel text-darwin-ink h-full outline-none"
        >
          <option value="zh">中文</option>
          <option value="en">English</option>
        </select>
        <button 
          type="button"
          @click="saveCalibration"
          class="w-full px-2.5 py-3 font-bold rounded-full bg-gradient-to-br from-darwin-amber to-darwin-orange text-black shadow-md"
        >
          {{ t.saveCalibration }}
        </button>
        <button 
          type="button"
          @click="refreshCrsf"
          class="col-span-2 lg:col-span-1 w-full px-2.5 py-3 font-bold border border-white/10 rounded-full bg-darwin-panel text-darwin-ink"
        >
          {{ t.refreshCrsf }}
        </button>
        <div class="col-span-2 lg:col-span-1 grid grid-cols-2 gap-2.5">
          <button
            type="button"
            @click="setLinkWireMode('dual')"
            :disabled="isSwitchingLinkMode"
            :class="['w-full px-2.5 py-3 font-bold border rounded-full transition-all', linkWireMode === 'dual' ? 'border-darwin-amber/60 bg-gradient-to-br from-darwin-amber to-darwin-orange text-black' : 'border-white/10 bg-darwin-panel text-darwin-ink']"
          >
            {{ t.dualWire }}
          </button>
          <button
            type="button"
            @click="setLinkWireMode('single')"
            :disabled="isSwitchingLinkMode"
            :class="['w-full px-2.5 py-3 font-bold border rounded-full transition-all', linkWireMode === 'single' ? 'border-darwin-amber/60 bg-gradient-to-br from-darwin-amber to-darwin-orange text-black' : 'border-white/10 bg-darwin-panel text-darwin-ink']"
          >
            {{ t.singleWire }}
          </button>
        </div>
      </div>
    </aside>

    <!-- Main Content -->
    <main class="grid content-start gap-6 p-4 lg:p-7 box-border min-w-0">
      <!-- Tabs Content -->
      <section v-if="currentTab === 'controls'" class="grid gap-5">
        <!-- Top Status Bar & Mode Switch -->
        <section class="p-5 lg:p-[18px_20px] rounded-[22px] border border-white/10 bg-darwin-panel shadow-xl bg-[radial-gradient(circle_at_top_left,rgba(245,166,35,0.1),transparent_34%),linear-gradient(180deg,rgba(45,45,45,0.9),rgba(35,35,35,0.9))]">
          <div class="grid lg:flex gap-3.5">
            <!-- Mode Switch -->
            <div class="flex justify-center p-1 border border-darwin-amber/20 rounded-full bg-white/5 h-fit">
              <button 
                v-for="mode in [{id: 0, l: 'HID'}, {id: 1, l: 'XBOX'}]" 
                :key="mode.id"
                type="button"
                @click="simMode = mode.id"
                :class="['min-w-[92px] px-4 py-[11px] rounded-full text-darwin-muted font-bold transition-all', { 'bg-gradient-to-br from-darwin-amber to-darwin-orange text-black shadow-md': simMode === mode.id }]"
              >
                {{ mode.l }}
              </button>
            </div>
            <!-- Status Cards -->
            <div class="flex-1 grid grid-cols-1 lg:grid-cols-2 gap-3.5 mt-3 lg:mt-0">
              <article class="px-4 py-3.5 border border-white/10 rounded-[20px] bg-white/5">
                <span class="text-darwin-muted text-[0.82rem] uppercase tracking-widest block mb-1">{{ t.statusRealtime }}</span>
                <strong class="text-[1.15rem] text-white block">{{ activeChannelsCount }}/8 Active</strong>
                <small class="text-darwin-muted">{{ channelRange.min }} ~ {{ channelRange.max }}</small>
              </article>
              <article class="px-4 py-3.5 border border-white/10 rounded-[20px] bg-white/5">
                <span class="text-darwin-muted text-[0.82rem] uppercase tracking-widest block mb-1">{{ t.statusLink }}</span>
                <strong class="text-[1.15rem] text-white block">{{ crsfStatus.isLinked ? t.linked : t.unlinked }}</strong>
                <small class="text-darwin-muted">RSSI {{ crsfStatus.rssi ?? '--' }} · LQ {{ crsfStatus.lq ?? '--' }}</small>
              </article>
            </div>
          </div>
        </section>

        <!-- Joystick & Channels -->
        <section class="grid grid-cols-1 lg:grid-cols-2 gap-5">
          <!-- Stick View -->
          <article class="p-6 rounded-[22px] border border-white/10 bg-darwin-panel shadow-lg">
            <h4 class="m-0 mb-4 text-darwin-amber text-[11px] font-bold uppercase tracking-widest">{{ t.stickView }}</h4>
            <div class="grid grid-cols-2 gap-3 justify-items-center">
              <Joystick :x="leftStick.x" :y="leftStick.y" :label="t.leftStick" />
              <Joystick :x="rightStick.x" :y="rightStick.y" :label="t.rightStick" />
            </div>
          </article>
          <!-- Primary Channels -->
          <article class="p-6 rounded-[22px] border border-white/10 bg-darwin-panel shadow-lg">
            <h4 class="m-0 mb-4 text-darwin-amber text-[11px] font-bold uppercase tracking-widest">{{ t.primaryChannels }}</h4>
            <div class="grid grid-cols-2 gap-3">
              <div v-for="ch in channels.slice(0, 4)" :key="ch.id" class="p-4 border border-white/10 rounded-[20px] bg-white/5">
                <span class="text-darwin-muted block mb-1">CH{{ ch.id }}</span>
                <strong class="text-[1.4rem] text-white block">{{ ch.mappedValue }}</strong>
              </div>
            </div>
          </article>
        </section>

        <!-- Calibration Panel -->
        <section class="p-6 rounded-[22px] border border-white/10 bg-darwin-panel">
          <h4 class="m-0 mb-5 text-darwin-amber text-[11px] font-bold uppercase tracking-widest">{{ t.channelCalibration }}</h4>
          <!-- Auto Calibration -->
          <div class="mb-5 p-4 border border-darwin-amber/20 rounded-[20px] bg-white/5">
            <strong class="text-white block mb-3">{{ t.autoCalibration }}</strong>
            <p class="text-darwin-muted text-sm mb-4">{{ t.autoCalibrationDesc }}</p>
            <button @click="showCalibrationModal = true" class="px-5 py-2.5 rounded-full bg-gradient-to-br from-darwin-amber to-darwin-orange text-black font-bold">
              {{ t.startAutoCalibration }}
            </button>
          </div>
          <!-- 16 Channel Sliders -->
          <div class="grid grid-cols-1 lg:grid-cols-2 gap-3.5">
            <AuxChannelSlider 
              v-for="ch in visibleChannels" 
              :key="ch.id"
              :channel-id="ch.id"
              :mapped-value="ch.mappedValue"
              v-model:raw-value="ch.rawValue"
              :cal="ch.cal"
              :t-raw-label="currentLang === 'zh' ? '原始值' : 'RAW'"
            />
          </div>
        </section>
      </section>

      <!-- CRSF Configurer -->
      <section v-else>
        <CrsfConfiguratorPanel 
          :menus="crsfMenus" 
          :status="crsfStatus" 
          :loading="isCrsfLoading" 
          :t="t"
          @refresh="refreshCrsf"
          @bind="handleCrsfBind"
          @command="handleCrsfCommand"
          @select-change="handleCrsfSelectChange"
        />
      </section>
    </main>

    <!-- WebSocket Singleton -->
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
import { ref, computed } from 'vue';
import { useRouter } from 'vue-router';
import Joystick from '../components/Joystick.vue';
import AuxChannelSlider from '../components/AuxChannelSlider.vue';
import CrsfConfiguratorPanel from '../components/CrsfConfiguratorPanel.vue';
import WebSocketConnection from '../components/WebSocketConnection.vue';
import CalibrationModal from '../components/CalibrationModal.vue';

// --- Constants & I18n ---
const SIM_MODE_HID = 0;
const SIM_MODE_XBOX = 1;

const translations = {
  zh: {
    controls: 'RC 面板',
    crsf: 'CRSF 菜单',
    eyebrowControls: '控制台',
    titleControls: '摇杆、通道与校准',
    eyebrowCrsf: '参数配置',
    titleCrsf: '接收机配置与命令入口',
    langLabel: '中文',
    saveCalibration: '保存校准',
    refreshCrsf: '刷新 CRSF',
    linkWireMode: 'CRSF 线制',
    dualWire: '双线',
    singleWire: '单线',
    refresh: '刷新',
    connection: '连接',
    online: '在线',
    offline: '离线',
    mode: '模式',
    crsfLabel: 'CRSF',
    statusRealtime: '实时状态',
    statusLink: '链路快照',
    linked: '已链路',
    unlinked: '未链路',
    stickView: '摇杆视图',
    leftStick: '左摇杆',
    rightStick: '右摇杆',
    primaryChannels: '主通道',
    channelCalibration: '通道校准',
    autoCalibration: '摇杆校准',
    autoCalibrationDesc: '像游戏手柄一样，通过推动和旋转摇杆完成校准。',
    startAutoCalibration: '开始校准',
    cancel: '取消',
    notConnectedAlert: '未连接到设备，无法保存。',
    savedAlert: '模式与校准数据已发送。',
    calibrationResultAlert: '自动校准结果已写入 CH1-CH4，请点“保存校准”同步到设备。',
    processing: '处理中...',
    startBind: '开始对频',
    link: '链路',
    signal: '信号',
    menuSync: '菜单同步',
    readOnly: '只读',
    command: 'Cmd',
    runCommand: '执行 Cmd',
    execute: '执行',
    folder: '文件夹',
    select: '选项',
    info: '信息',
    text: '文本',
    general: '通用'
  }
};

const t = translations.zh;

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
};

// --- State ---
const ws = ref(null);
const wsBuffer = ref('');
const isConnected = ref(false);
const currentTab = ref('controls');
const router = useRouter();
const currentLang = ref('zh');

const simMode = ref(SIM_MODE_HID);
const isCrsfLoading = ref(false);
const linkWireMode = ref('dual');
const isSwitchingLinkMode = ref(false);

const crsfStatus = ref({
  isReady: true,
  isLinked: false,
  rssi: -91,
  lq: 100,
  loadedParams: 20,
  totalParams: 20,
  deviceLabel: ''
});

const isInitialSynced = ref(false);
const showCalibrationModal = ref(false);

const channels = ref(Array.from({ length: 16 }, (_, i) => ({
  id: i + 1,
  mappedValue: 1500,
  rawValue: 2048,
  cal: { min: 1000, mid: 1500, max: 2000 }
})));

// --- UI Helpers ---
const navItems = computed(() => [
  { id: 'controls', label: t.controls },
  { id: 'crsf', label: t.crsf }
]);

const globalStatus = computed(() => [
  { label: t.connection, value: isConnected.value ? t.online : t.offline, accent: isConnected.value },
  { label: t.mode, value: simMode.value === SIM_MODE_XBOX ? 'XBOX' : 'HID', accent: true },
  { label: t.crsfLabel, value: `${crsfStatus.value.loadedParams ?? 0}/${crsfStatus.value.totalParams ?? 0}`, accent: crsfStatus.value.isReady }
]);

const leftStick = computed(() => ({
  x: (channels.value[3].mappedValue - 1500) / 5,
  y: (channels.value[2].mappedValue - 1500) / 5
}));

const rightStick = computed(() => ({
  x: (channels.value[0].mappedValue - 1500) / 5,
  y: (channels.value[1].mappedValue - 1500) / 5
}));

const channelRange = computed(() => {
  const vals = channels.value.map(c => c.mappedValue);
  return { min: Math.min(...vals), max: Math.max(...vals) };
});

const visibleChannels = computed(() => {
  return channels.value.filter(c => c.id <= 4 || (c.id >= 9 && c.id <= 12));
});

const activeChannelsCount = computed(() => {
  return visibleChannels.value.filter(c => c.mappedValue !== 1500 || c.rawValue !== 2048).length;
});

// --- CRSF Translation Map ---
const crsfLabelMap = {
  "Packet Rate": { zh: "发包速率", en: "Packet Rate" },
  "Telem Ratio": { zh: "回传比例", en: "Telem Ratio" },
  "Switch Mode": { zh: "开关模式", en: "Switch Mode" },
  "Link Mode": { zh: "链路模式", en: "Link Mode" },
  "Model Match": { zh: "模型匹配", en: "Model Match" },
  "TX Power (100mW)": { zh: "发射功率（100mW）", en: "TX Power (100mW)" },
  "Max Power": { zh: "最大发射功率", en: "Max Power" },
  "Dynamic": { zh: "动态功率", en: "Dynamic" },
  "VTX Administrator": { zh: "图传设置", en: "VTX Administrator" },
  "Band": { zh: "频段", en: "Band" },
  "Channel": { zh: "频道", en: "Channel" },
  "Pwr Lvl": { zh: "功率等级", en: "Pwr Lvl" },
  "Pitmode": { zh: "陷波模式", en: "Pitmode" },
  "Send VTx": { zh: "发送图传设置", en: "Send VTx" },
  "WiFi Connectivity": { zh: "WiFi 连接", en: "WiFi Connectivity" },
  "Enable WiFi": { zh: "开启 WiFi", en: "Enable WiFi" },
  "Enable Rx WiFi": { zh: "开启接收机 WiFi", en: "Enable Rx WiFi" },
  "Bind": { zh: "对频", en: "Bind" },
  "Bad/Good": { zh: "坏包/好包", en: "Bad/Good" }
};

function translateCrsfLabel(label) {
  if (!label) return label;
  const entry = crsfLabelMap[label];
  return (entry && entry[currentLang.value]) || label;
}

function normalizeCrsfItem(item) {
  return {
    id: Number(item.id),
    parentId: Number(item.parentId ?? item.parent_id ?? 0),
    type: item.type ?? item.kind ?? 0,
    name: translateCrsfLabel(item.name),
    value: Number.isFinite(Number(item.value)) ? Number(item.value) : 0,
    options: item.options ?? item.content ?? ''
  };
}

const MOCK_CRSF_DATA = [
  {id:1,parentId:0,type:9,name:`Packet Rate`,value:3,options:`50Hz(-115dBm);100Hz Full(-112dBm);150Hz(-112dBm);250Hz(-108dBm);333Hz Full(-105dBm);500Hz(-105dBm);D250(-104dBm);D500(-104dBm);F500(-104dBm)`},
  {id:2,parentId:0,type:9,name:`Telem Ratio`,value:0,options:`Std;Off;1:128;1:64;1:32;1:16;1:8;1:4;1:2;Race`},
  {id:3,parentId:0,type:9,name:`Switch Mode`,value:0,options:`Wide;Hybrid`},
  {id:4,parentId:0,type:9,name:`Link Mode`,value:0,options:`Normal;MAVLink`},
  {id:5,parentId:0,type:9,name:`Model Match`,value:0,options:`Off;On`},
  {id:6,parentId:0,type:11,name:`TX Power (100mW)`,value:0,options:``},
  {id:7,parentId:6,type:9,name:`Max Power`,value:1,options:`10;100`},
  {id:8,parentId:6,type:9,name:`Dynamic`,value:0,options:`Off;Dyn;AUX9;AUX10;AUX11;AUX12`},
  {id:9,parentId:0,type:11,name:`VTX Administrator`,value:0,options:``},
  {id:10,parentId:9,type:9,name:`Band`,value:0,options:`Off;A;B;E;F;R;L`},
  {id:11,parentId:9,type:0,name:`Channel`,value:0,options:`1-8`},
  {id:12,parentId:9,type:9,name:`Pwr Lvl`,value:0,options:`-;1;2;3;4;5;6;7;8`},
  {id:13,parentId:9,type:9,name:`Pitmode`,value:0,options:`Off;On;AUX1:Lo;AUX1:Hi;AUX2:Lo;AUX2:Hi;AUX3:Lo;AUX3:Hi;AUX4:Lo;AUX4:Hi;AUX5:Lo;AUX5:Hi`},
  {id:14,parentId:9,type:13,name:`Send VTx`,value:0,options:``},
  {id:15,parentId:0,type:11,name:`WiFi Connectivity`,value:0,options:``},
  {id:16,parentId:15,type:13,name:`Enable WiFi`,value:0,options:``},
  {id:17,parentId:15,type:13,name:`Enable Rx WiFi`,value:0,options:``},
  {id:18,parentId:0,type:13,name:`Bind`,value:0,options:``},
  {id:19,parentId:0,type:12,name:`Bad/Good`,value:0,options:`0/1`},
  {id:20,parentId:0,type:12,name:`ver.unknown ISM2G4`,value:0,options:`40555e`}
];

const crsfMenus = ref(MOCK_CRSF_DATA.map(normalizeCrsfItem));

// --- Methods ---
function changeLanguage(event) {
  const lang = event.target.value;
  localStorage.setItem('user_lang', lang);
  router.push('/' + lang);
}

function saveCalibration() {
  if (!ws.value) {
    window.alert(t.notConnectedAlert);
    return;
  }
  ws.value.sendData(`M:${simMode.value}`);
  const calCmd = channels.value
    .map(c => `${c.id},${c.cal.min},${c.cal.mid},${c.cal.max}`)
    .join(';');
  ws.value.sendData(`C:${calCmd}`);
  window.alert(t.savedAlert);
}

function refreshCrsf() {
  isCrsfLoading.value = true;
  ws.value?.sendData('CRSF_REFRESH');
}

function setLinkWireMode(mode) {
  if (!ws.value || isSwitchingLinkMode.value || linkWireMode.value === mode) {
    return;
  }
  isSwitchingLinkMode.value = true;
  linkWireMode.value = mode;
  ws.value.sendData(`CRSF_LINK:${mode === 'single' ? 'SINGLE' : 'DUAL'}`);
  window.setTimeout(() => {
    ws.value?.sendData('CRSF_SNAPSHOT');
    isSwitchingLinkMode.value = false;
  }, 450);
}

function handleCrsfBind(item) {
  isCrsfLoading.value = true;
  ws.value?.sendData(`CRSF_BIND:${item.id}`);
  setTimeout(() => isCrsfLoading.value = false, 600);
}

function handleCrsfCommand(item) {
  isCrsfLoading.value = true;
  ws.value?.sendData(`CRSF_COMMAND:${item.id}`);
  setTimeout(() => isCrsfLoading.value = false, 600);
}

function handleCrsfSelectChange(node) {
  // Optimistically update
  crsfMenus.value = crsfMenus.value.map(m => m.id === node.id ? { ...m, value: node.value } : m);
  ws.value?.sendData(`CRSF_WRITE:${node.id}:${node.value}`);
}

// --- Calibration ---
function onCalibrationResult(results) {
  // results: { 0: {min,mid,max}, 1: {min,mid,max} }  (index 0=CH1, 1=CH2 for right; 2=CH3, 3=CH4 for left)
  Object.entries(results).forEach(([idx, cal]) => {
    const ch = channels.value[parseInt(idx)];
    if (ch) {
      ch.cal.min = cal.min;
      ch.cal.mid = cal.mid;
      ch.cal.max = cal.max;
    }
  });
  window.alert(t.calibrationResultAlert);
}

// --- WebSocket Handlers ---
function onWsData(data) {
  wsBuffer.value += data;
  const lines = wsBuffer.value.split(/\r?\n/);
  wsBuffer.value = lines.pop() ?? '';

  lines.map(l => l.trim()).filter(Boolean).forEach(line => {
    if (line.startsWith('CRSF_SNAPSHOT:')) {
      try {
        const payload = JSON.parse(line.slice('CRSF_SNAPSHOT:'.length));
        updateCrsfState(payload);
      } catch (e) { console.error('CRSF_SNAPSHOT parse error', e); }
      return;
    }
    if (line.startsWith('CRSF_STATUS:')) {
      try {
        const payload = JSON.parse(line.slice('CRSF_STATUS:'.length));
        updateCrsfState({ status: payload });
      } catch (e) { console.error('CRSF_STATUS parse error', e); }
      return;
    }
    if (line.startsWith('CRSF_MENU:')) {
      try {
        const payload = JSON.parse(line.slice('CRSF_MENU:'.length));
        updateCrsfState({ menus: payload });
      } catch (e) { console.error('CRSF_MENU parse error', e); }
      return;
    }

    // 1. JSON (CRSF Status/Menu)
    if (line.startsWith('{')) {
      try {
        const msg = JSON.parse(line);
        if (msg.type === 'crsf_snapshot' || msg.type === 'crsf_status' || msg.type === 'crsf_menu') {
          updateCrsfState(msg.status ? msg : { status: msg, menus: msg.menus });
        }
      } catch (e) { console.error('JSON parse error', e); }
      return;
    }

    // 2. Calibration Info (C:min,mid,max;...)
    if (line.startsWith('C:')) {
      parseCalibration(line);
      return;
    }

    // 3. Mode/Channels
    if (!parseModeLine(line)) {
      parseChannelsLine(line);
    }
  });
}

function updateCrsfState(data) {
  if (data.status) {
    const s = data.status;
    crsfStatus.value = {
      ...crsfStatus.value,
      isReady: s.isReady ?? s.is_ready ?? crsfStatus.value.isReady,
      isLinked: s.isLinked ?? s.is_linked ?? crsfStatus.value.isLinked,
      rssi: s.rssi ?? crsfStatus.value.rssi,
      lq: s.lq ?? crsfStatus.value.lq,
      loadedParams: s.loadedParams ?? s.loaded_params ?? crsfStatus.value.loadedParams,
      totalParams: s.totalParams ?? s.total_params ?? crsfStatus.value.totalParams
    };
    const wireMode = (s.wireMode ?? s.wire_mode ?? '').toString().toLowerCase();
    if (wireMode === 'single' || wireMode === 'dual') {
      linkWireMode.value = wireMode;
    }
  }
  if (Array.isArray(data.menus)) {
    crsfMenus.value = data.menus.map(normalizeCrsfItem).filter(m => m.id > 0);
  }
  isInitialSynced.value = true;
  if (crsfStatus.value.totalParams > 0 && crsfStatus.value.loadedParams >= crsfStatus.value.totalParams) {
    isCrsfLoading.value = false;
  }
}

function parseCalibration(line) {
  line.slice(2).split(';').forEach(segment => {
    const parts = segment.split(',').map(p => parseInt(p.trim(), 10));
    if (parts.length < 4) return;
    const [id, min, mid, max] = parts;
    const ch = channels.value.find(c => c.id === id);
    if (ch && !isNaN(min) && !isNaN(mid) && !isNaN(max)) {
      ch.cal.min = min;
      ch.cal.mid = mid;
      ch.cal.max = max;
    }
  });
}

function parseModeLine(line) {
  const prefix = ['M:', 'MODE:', 'SIM_MODE:'].find(p => line.startsWith(p));
  if (!prefix) return false;
  const m = parseInt(line.slice(prefix.length).trim(), 10);
  if (m === SIM_MODE_HID || m === SIM_MODE_XBOX) {
    simMode.value = m;
  }
  return true;
}

function parseChannelsLine(line) {
  const values = line.split(',').map(v => v.trim()).filter(Boolean);
  if (values.length === channels.value.length) {
    values.forEach((v, i) => {
      const ch = channels.value[i];
      if (!ch) return;
      const parts = v.split(':');
      if (parts.length === 2) {
        const mapped = parseInt(parts[0], 10);
        const raw = parseInt(parts[1], 10);
        if (!isNaN(mapped) && !isNaN(raw)) {
          ch.mappedValue = mapped;
          ch.rawValue = raw;
        }
      } else {
        const raw = parseInt(v, 10);
        if (!isNaN(raw)) {
          ch.rawValue = raw;
          // Simple lerp for mapped if not calibrated (fallback)
          if (raw >= 800 && raw <= 2200) ch.mappedValue = raw;
        }
      }
    });
  }
}

function requestCalibration() {
  ws.value?.sendData('CRSF_SNAPSHOT');
}
</script>
