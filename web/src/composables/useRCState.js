import { ref, computed } from 'vue'
import { useRouter } from 'vue-router'

const SIM_MODE_HID = 0
const SIM_MODE_XBOX = 1
const STICK_MODE_1 = 1  // 日本手: Pitch on left, Throttle on right
const STICK_MODE_2 = 2  // 美国手: Throttle on left, Pitch on right

// ========== 模块级单例状态（所有 useRCState 调用共享） ==========

// --- WebSocket ---
const ws = ref(null)
const wsBuffer = ref('')
const isConnected = ref(false)

// --- UI State ---
const currentTab = ref('dashboard')
const currentLang = ref('zh')
const simMode = ref(SIM_MODE_HID)
const isDarkMode = ref(true)

// --- Theme ---
function initTheme() {
  const saved = localStorage.getItem('theme')
  if (saved === 'light') {
    isDarkMode.value = false
    document.documentElement.classList.remove('dark')
  } else if (saved === 'dark') {
    isDarkMode.value = true
    document.documentElement.classList.add('dark')
  } else {
    isDarkMode.value = true
    document.documentElement.classList.add('dark')
  }
}

function toggleTheme() {
  isDarkMode.value = !isDarkMode.value
  if (isDarkMode.value) {
    document.documentElement.classList.add('dark')
    localStorage.setItem('theme', 'dark')
  } else {
    document.documentElement.classList.remove('dark')
    localStorage.setItem('theme', 'light')
  }
}

// Initialize theme immediately
initTheme()
const showCalibrationModal = ref(false)
const configSubTab = ref('props') // 'props' | 'mapping' | 'stick'
const stickMode = ref(STICK_MODE_2) // 默认美国手

// --- 按键触发模式: [SA(索引0), SB(1), SC(2), SD(3)] ---
// SA/SD: 0=触摸 1=单击 2=双击
// SB/SC: 0=三态 1=二态
const btnCfg = ref([0, 0, 0, 0])

// --- Channel Mapping (switches only, CH5-CH8) ---
const channelMapping = ref([
  { channel: 5,  default: 'SA', current: 'SA' },
  { channel: 6,  default: 'SB', current: 'SB' },
  { channel: 7,  default: 'SC', current: 'SC' },
  { channel: 8,  default: 'SD', current: 'SD' },
])

const availableSwitches = ['SA', 'SB', 'SC', 'SD', 'None']

// --- CRSF ---
const isCrsfLoading = ref(false)
const linkWireMode = ref('dual')
const isSwitchingLinkMode = ref(false)
const crsfStatus = ref({
  isReady: true,
  isLinked: false,
  rssi: -91,
  lq: 100,
  loadedParams: 20,
  totalParams: 20,
  deviceLabel: ''
})
const isInitialSynced = ref(false)

// --- Channels ---
const channels = ref(Array.from({ length: 16 }, (_, i) => ({
  id: i + 1,
  mappedValue: 1500,
  rawValue: 2048,
  cal: { min: 1000, mid: 1500, max: 2000 }
})))

// --- EPA/REV ---
const epaData = ref(Array.from({ length: 16 }, (_, i) => ({
  ch: i + 1, pos: 100, neg: 100
})))
const revMask = ref(0)

// --- CRSF Label Map ---
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
}

// --- Computed ---
// Mode 2 (美国手): CH1=Roll, CH2=Pitch(右Y), CH3=Throttle(左Y), CH4=Yaw
// Mode 1 (日本手): CH1=Roll, CH2=Throttle(左Y), CH3=Pitch(右Y), CH4=Yaw
//   (ESP32 在 Mode 1 时交换了 src[1]/src[2], 所以左Y数据在 CH2, 右Y数据在 CH3)
const leftStick = computed(() => {
  if (stickMode.value === STICK_MODE_1) {
    return {
      x: (channels.value[3].mappedValue - 1500) / 5,  // CH4 Yaw = 左手柄 X
      y: (channels.value[1].mappedValue - 1500) / 5,  // CH2 = 左手柄 Y (油门电位器)
    }
  }
  return {
    x: (channels.value[3].mappedValue - 1500) / 5,  // CH4 Yaw = 左手柄 X
    y: (channels.value[2].mappedValue - 1500) / 5,  // CH3 Throttle = 左手柄 Y
  }
})

const rightStick = computed(() => {
  if (stickMode.value === STICK_MODE_1) {
    return {
      x: (channels.value[0].mappedValue - 1500) / 5,  // CH1 Roll = 右手柄 X
      y: (channels.value[2].mappedValue - 1500) / 5,  // CH3 = 右手柄 Y (俯仰电位器)
    }
  }
  return {
    x: (channels.value[0].mappedValue - 1500) / 5,  // CH1 Roll = 右手柄 X
    y: (channels.value[1].mappedValue - 1500) / 5,  // CH2 Pitch = 右手柄 Y
  }
})

const channelRange = computed(() => {
  const vals = channels.value.map(c => c.mappedValue)
  return { min: Math.min(...vals), max: Math.max(...vals) }
})

const visibleChannels = computed(() =>
  channels.value.filter(c => c.id <= 8)
)

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
]

const crsfMenus = ref(MOCK_CRSF_DATA.map(normalizeCrsfItem))

// CRSF Helpers (模块级，供 normalizeCrsfItem 引用)
function translateCrsfLabel(label) {
  if (!label) return label
  const entry = crsfLabelMap[label]
  return (entry && entry[currentLang.value]) || label
}

function normalizeCrsfItem(item) {
  return {
    id: Number(item.id),
    parentId: Number(item.parentId ?? item.parent_id ?? 0),
    type: item.type ?? item.kind ?? 0,
    name: translateCrsfLabel(item.name),
    value: Number.isFinite(Number(item.value)) ? Number(item.value) : 0,
    options: item.options ?? item.content ?? ''
  }
}

let _router = null

export function useRCState() {
  if (!_router) _router = useRouter()

  // --- Methods ---
  function changeLanguage(lang) {
    currentLang.value = lang
    localStorage.setItem('user_lang', lang)
    _router.push('/' + lang)
  }

  function refreshCrsf() {
    isCrsfLoading.value = true
    ws.value?.sendData('CRSF_REFRESH')
  }

  function setLinkWireMode(mode) {
    if (!ws.value || isSwitchingLinkMode.value || linkWireMode.value === mode) return
    isSwitchingLinkMode.value = true
    linkWireMode.value = mode
    ws.value.sendData(`CRSF_LINK:${mode === 'single' ? 'SINGLE' : 'DUAL'}`)
    setTimeout(() => {
      ws.value?.sendData('CRSF_SNAPSHOT')
      isSwitchingLinkMode.value = false
    }, 450)
  }

  function handleCrsfBind(item) {
    isCrsfLoading.value = true
    ws.value?.sendData(`CRSF_BIND:${item.id}`)
    setTimeout(() => isCrsfLoading.value = false, 600)
  }

  function handleCrsfCommand(item) {
    isCrsfLoading.value = true
    ws.value?.sendData(`CRSF_COMMAND:${item.id}`)
    setTimeout(() => isCrsfLoading.value = false, 600)
  }

  function handleCrsfSelectChange(node) {
    crsfMenus.value = crsfMenus.value.map(m => m.id === node.id ? { ...m, value: node.value } : m)
    ws.value?.sendData(`CRSF_WRITE:${node.id}:${node.value}`)
  }

  /** 用校准参数将原始 ADC 值映射到 1000-2000 */
function mapRawToMapped(raw, cal) {
  if (!cal || cal.min === cal.mid || cal.mid === cal.max) return 1500;
  if (raw <= cal.mid) return 1000 + (raw - cal.min) / (cal.mid - cal.min) * 500;
  return 1500 + (raw - cal.mid) / (cal.max - cal.mid) * 500;
}

function onCalibrationResult(results) {
  // 1. 更新本地状态
  Object.entries(results).forEach(([idx, cal]) => {
    const ch = channels.value[parseInt(idx)]
    if (ch) {
      ch.cal.min = cal.min
      ch.cal.mid = cal.mid
      ch.cal.max = cal.max
    }
  })
  // 2. 发给 ESP32 更新 limit[] + 持久化到 NVS
  if (ws.value) {
    const calCmd = Object.entries(results)
      .map(([idx, cal]) => `${parseInt(idx) + 1},${cal.min},${cal.mid},${cal.max}`)
      .join(';')
    ws.value.sendData(`C:${calCmd}`)
    ws.value.sendData('SAVE_NVS')
  }
}

  // --- WebSocket Handlers ---
  function onWsData(data) {
    wsBuffer.value += data
    const lines = wsBuffer.value.split(/\r?\n/)
    wsBuffer.value = lines.pop() ?? ''

    lines.map(l => l.trim()).filter(Boolean).forEach(line => {
      if (line.startsWith('CRSF_SNAPSHOT:')) {
        try {
          const payload = JSON.parse(line.slice('CRSF_SNAPSHOT:'.length))
          updateCrsfState(payload)
        } catch (e) { console.error('CRSF_SNAPSHOT parse error', e) }
        return
      }
      if (line.startsWith('CRSF_STATUS:')) {
        try {
          const payload = JSON.parse(line.slice('CRSF_STATUS:'.length))
          updateCrsfState({ status: payload })
        } catch (e) { console.error('CRSF_STATUS parse error', e) }
        return
      }
      if (line.startsWith('CRSF_MENU:')) {
        try {
          const payload = JSON.parse(line.slice('CRSF_MENU:'.length))
          updateCrsfState({ menus: payload })
        } catch (e) { console.error('CRSF_MENU parse error', e) }
        return
      }
      if (line.startsWith('{')) {
        try {
          const msg = JSON.parse(line)
          if (msg.type === 'crsf_snapshot' || msg.type === 'crsf_status' || msg.type === 'crsf_menu') {
            updateCrsfState(msg.status ? msg : { status: msg, menus: msg.menus })
          }
        } catch (e) { console.error('JSON parse error', e) }
        return
      }
      if (line.startsWith('C:')) {
        parseCalibration(line)
        return
      }
      if (line.startsWith('EPA:')) {
        parseEpa(line)
        return
      }
      if (line.startsWith('REV:')) {
        parseRev(line)
        return
      }
      if (line.startsWith('MAP:')) {
        parseMap(line)
        return
      }
      if (line.startsWith('MAP_OK')) {
        mapWriteState.value = 'ok'
        return
      }
      if (line.startsWith('STICK_MODE:')) {
        const mode = parseInt(line.slice('STICK_MODE:'.length).trim(), 10)
        if (mode === 1 || mode === 2) {
          stickMode.value = mode
        }
        return
      }
      if (line.startsWith('BTN:')) {
        const vals = line.slice(4).trim().split(',').map(v => parseInt(v, 10))
        if (vals.length === 4 && vals.every(v => !isNaN(v))) {
          btnCfg.value = vals
        }
        return
      }
      if (!parseModeLine(line)) {
        parseChannelsLine(line)
      }
    })
  }

  function updateCrsfState(data) {
    if (data.status) {
      const s = data.status
      crsfStatus.value = {
        ...crsfStatus.value,
        isReady: s.isReady ?? s.is_ready ?? crsfStatus.value.isReady,
        isLinked: s.isLinked ?? s.is_linked ?? crsfStatus.value.isLinked,
        rssi: s.rssi ?? crsfStatus.value.rssi,
        lq: s.lq ?? crsfStatus.value.lq,
        loadedParams: s.loadedParams ?? s.loaded_params ?? crsfStatus.value.loadedParams,
        totalParams: s.totalParams ?? s.total_params ?? crsfStatus.value.totalParams
      }
      const wireMode = (s.wireMode ?? s.wire_mode ?? '').toString().toLowerCase()
      if (wireMode === 'single' || wireMode === 'dual') {
        linkWireMode.value = wireMode
      }
    }
    if (Array.isArray(data.menus)) {
      crsfMenus.value = data.menus.map(normalizeCrsfItem).filter(m => m.id > 0)
    }
    isInitialSynced.value = true
    if (crsfStatus.value.totalParams > 0 && crsfStatus.value.loadedParams >= crsfStatus.value.totalParams) {
      isCrsfLoading.value = false
    }
  }

  function parseCalibration(line) {
    line.slice(2).split(';').forEach(segment => {
      const parts = segment.split(',').map(p => parseInt(p.trim(), 10))
      if (parts.length < 4) return
      const [id, min, mid, max] = parts
      const ch = channels.value.find(c => c.id === id)
      if (ch && !isNaN(min) && !isNaN(mid) && !isNaN(max)) {
        ch.cal.min = min
        ch.cal.mid = mid
        ch.cal.max = max
      }
    })
  }

  function parseEpa(line) {
    line.slice(4).split(';').forEach(segment => {
      const parts = segment.split(',').map(p => parseInt(p.trim(), 10))
      if (parts.length < 3) return
      const [ch, pos, neg] = parts
      const e = epaData.value.find(e => e.ch === ch)
      if (e && !isNaN(pos) && !isNaN(neg)) {
        e.pos = pos
        e.neg = neg
      }
    })
  }

  function parseRev(line) {
    const val = parseInt(line.slice(4).trim(), 10)
    if (!isNaN(val)) revMask.value = val
  }

  const SOURCE_NAMES = { 4: 'SA', 5: 'SB', 6: 'SC', 7: 'SD' }
  const SOURCE_IDS   = { SA: 4, SB: 5, SC: 6, SD: 7 }

  function parseMap(line) {
    line.slice(4).split(';').forEach(segment => {
      const parts = segment.split(',').map(p => p.trim())
      if (parts.length < 2) return
      const ch = parseInt(parts[0], 10)
      const src = parseInt(parts[1], 10)
      if (isNaN(ch) || isNaN(src)) return
      const m = channelMapping.value.find(m => m.channel === ch)
      if (m) {
        m.current = SOURCE_NAMES[src] || (src < 16 ? String(src) : 'None')
      }
    })
  }

  function parseModeLine(line) {
    const prefix = ['M:', 'MODE:', 'SIM_MODE:'].find(p => line.startsWith(p))
    if (!prefix) return false
    const m = parseInt(line.slice(prefix.length).trim(), 10)
    if (m === SIM_MODE_HID || m === SIM_MODE_XBOX) {
      simMode.value = m
    }
    return true
  }

  function parseChannelsLine(line) {
    const values = line.split(',').map(v => v.trim()).filter(Boolean)
    if (values.length === channels.value.length) {
      values.forEach((v, i) => {
        const ch = channels.value[i]
        if (!ch) return
        const parts = v.split(':')
        if (parts.length === 2) {
          const mapped = parseInt(parts[0], 10)
          const raw = parseInt(parts[1], 10)
          if (!isNaN(mapped) && !isNaN(raw)) {
            ch.mappedValue = mapped
            ch.rawValue = raw
          }
        } else {
          const raw = parseInt(v, 10)
          if (!isNaN(raw)) {
            ch.rawValue = raw
            if (raw >= 800 && raw <= 2200) ch.mappedValue = raw
          }
        }
      })
    }
  }

  function requestCalibration() {
    ws.value?.sendData('GET_CAL')
  }

  // --- Channel Mapping Methods ---
  const mapWriteState = ref('idle') // 'idle' | 'sending' | 'ok'

  function updateChannelMapping(idx, newSwitch) {
    if (idx >= 0 && idx < channelMapping.value.length) {
      channelMapping.value[idx].current = newSwitch
    }
  }

  function resetChannelMapping() {
    channelMapping.value.forEach(m => { m.current = m.default })
    if (!ws.value) return
    const cmd = 'MAP:' + channelMapping.value.map(m => `${m.channel},${m.current}`).join(';')
    mapWriteState.value = 'sending'
    ws.value.sendData(cmd)
  }

  function writeChannelMapping() {
    if (!ws.value) return
    const cmd = 'MAP:' + channelMapping.value.map(m => `${m.channel},${m.current}`).join(';')
    mapWriteState.value = 'sending'
    ws.value.sendData(cmd)
  }

  // --- Button Trigger Mode Methods ---
  function setBtnCfg(idx, val) {
    if (idx >= 0 && idx < 4) {
      btnCfg.value = [...btnCfg.value.slice(0, idx), val, ...btnCfg.value.slice(idx + 1)]
    }
    ws.value?.sendData(`BTN:${btnCfg.value[0]},${btnCfg.value[1]},${btnCfg.value[2]},${btnCfg.value[3]}`)
  }

  function resetBtnCfg() {
    btnCfg.value = [0, 0, 0, 0]
    ws.value?.sendData('BTN:0,0,0,0')
  }

  // --- Stick Mode ---
  function setStickMode(mode) {
    stickMode.value = mode
    ws.value?.sendData(`STICK_MODE:${mode}`)
  }

  function setEpa(ch, pos, neg) {
    const e = epaData.value.find(e => e.ch === ch)
    if (e) {
      e.pos = pos
      e.neg = neg
    }
    ws.value?.sendData(`EPA:${ch},${pos},${neg}`)
  }

  function setRev(ch, val) {
    if (val)
      revMask.value |= (1 << (ch - 1))
    else
      revMask.value &= ~(1 << (ch - 1))
    ws.value?.sendData(`REV:${ch},${val ? 1 : 0}`)
  }

  return {
    // state
    ws,
    isConnected,
    currentTab,
    currentLang,
    simMode,
    isDarkMode,
    showCalibrationModal,
    configSubTab,
    stickMode,
    btnCfg,
    channelMapping,
    availableSwitches,
    isCrsfLoading,
    linkWireMode,
    isSwitchingLinkMode,
    crsfStatus,
    channels,
    crsfMenus,
    epaData,
    revMask,

    // computed
    leftStick,
    rightStick,
    channelRange,
    visibleChannels,

    // methods
    toggleTheme,
    changeLanguage,
    refreshCrsf,
    setLinkWireMode,
    handleCrsfBind,
    handleCrsfCommand,
    handleCrsfSelectChange,
    onCalibrationResult,
    onWsData,
    requestCalibration,
    mapWriteState,
    updateChannelMapping,
    resetChannelMapping,
    writeChannelMapping,
    setStickMode,
    setBtnCfg,
    resetBtnCfg,
    setEpa,
    setRev,
  }
}
