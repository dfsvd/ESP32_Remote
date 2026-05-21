<template>
  <Teleport to="body">
    <div v-if="visible" class="fixed inset-0 z-50 flex items-center justify-center bg-black/70" @click.self="onCancel">
      <div class="bg-[var(--theme-panel)] border border-[var(--theme-border)] rounded-3xl p-8 w-[420px] select-none">
        <!-- Step Progress -->
        <div class="flex items-center justify-center gap-2 mb-6">
          <div v-for="(s, i) in steps" :key="i" class="flex items-center gap-2">
            <div :class="['w-7 h-7 rounded-full flex items-center justify-center text-xs font-bold transition-colors', i <= stepIndex ? 'bg-darwin-amber text-black' : 'bg-[var(--theme-bg-hover)] text-darwin-muted']">{{ i + 1 }}</div>
            <div v-if="i < steps.length - 1" :class="['w-8 h-px transition-colors', i < stepIndex ? 'bg-darwin-amber' : 'bg-[var(--theme-border)]']"></div>
          </div>
        </div>

        <!-- Joystick Visual -->
        <div class="relative w-[240px] h-[240px] mx-auto mb-6">
          <svg viewBox="0 0 240 240" class="w-full h-full">
            <!-- Outer boundary -->
            <circle cx="120" cy="120" r="100" fill="none" stroke="var(--theme-svg-stroke)" stroke-width="2"/>
            <circle cx="120" cy="120" r="68" fill="none" stroke="var(--theme-svg-stroke-subtle)" stroke-width="1"/>
            <!-- Crosshair -->
            <line x1="120" y1="20" x2="120" y2="220" stroke="var(--theme-svg-stroke-faint)" stroke-width="1"/>
            <line x1="20" y1="120" x2="220" y2="120" stroke="var(--theme-svg-stroke-faint)" stroke-width="1"/>
            <!-- Stick dot -->
            <circle :cx="120 + displayPos.x" :cy="120 + displayPos.y" r="14" fill="#F5A623" opacity="0.9" filter="url(#glow)"/>
            <defs>
              <filter id="glow">
                <feGaussianBlur stdDeviation="3" result="blur"/>
                <feMerge><feMergeNode in="blur"/><feMergeNode in="SourceGraphic"/></feMerge>
              </filter>
            </defs>
            <!-- Direction arrows (none for current steps) -->
          </svg>
          <!-- Step-specific overlays -->
          <div v-if="step === 'detect' && !detected" class="absolute inset-0 flex items-center justify-center">
            <div class="text-darwin-amber text-4xl animate-pulse">⟳</div>
          </div>
        </div>

        <!-- Debug: 显示校准捕获的原始 ADC 值 -->
        <div v-if="step !== 'detect'" class="text-center text-xs text-darwin-muted mb-2 font-mono leading-relaxed">
          <div>X 轴: 原始值={{ selectedStick ? (selectedStick === 'left' ? (channels[3]?.rawValue ?? '?') : (channels[0]?.rawValue ?? '?')) : '?' }} | 已捕获 min={{ rawCaptures.minX }} mid={{ capturedCenter.x }} max={{ rawCaptures.maxX }}</div>
          <div>Y 轴: 原始值={{ selectedStick ? (selectedStick === 'left' ? (channels[2]?.rawValue ?? '?') : (channels[1]?.rawValue ?? '?')) : '?' }} | 已捕获 min={{ rawCaptures.minY }} mid={{ capturedCenter.y }} max={{ rawCaptures.maxY }}</div>
        </div>

        <!-- Instruction -->
        <p class="text-center text-[var(--theme-text)] text-base mb-1 font-medium">{{ instruction }}</p>
        <p v-if="hint" class="text-center text-darwin-muted text-sm mb-6">{{ hint }}</p>
        <p v-else class="mb-6"></p>

        <!-- Completion Stats -->
        <div v-if="step === 'complete'" class="grid grid-cols-2 gap-3 mb-6">
          <div v-for="(axis, i) in calibratedAxes" :key="i" class="p-3 border border-[var(--theme-border)] rounded-xl bg-[var(--theme-bg-subtle)]">
            <span class="text-darwin-muted text-xs block mb-1">{{ axis.label }}</span>
            <span class="text-[var(--theme-text)] text-sm">min {{ axis.min }} · max {{ axis.max }}</span>
          </div>
        </div>

        <!-- Buttons -->
        <div class="flex justify-center gap-4">
          <button v-if="step !== 'complete'" @click="onCancel" class="px-6 py-2.5 rounded-full border border-[var(--theme-border)] text-darwin-ink bg-[var(--theme-bg-subtle)] hover:bg-[var(--theme-bg-hover)]">取消</button>
          <button v-if="step === 'center'" @click="onNextCenter" class="px-6 py-2.5 rounded-full bg-gradient-to-br from-darwin-amber to-darwin-orange text-black font-bold">下一步</button>
          <button v-if="step === 'rotate'" @click="onNextRotate" class="px-6 py-2.5 rounded-full bg-gradient-to-br from-darwin-amber to-darwin-orange text-black font-bold">下一步</button>
          <button v-if="step === 'complete'" @click="onConfirm" class="px-6 py-2.5 rounded-full bg-gradient-to-br from-darwin-amber to-darwin-orange text-black font-bold">完成</button>
        </div>
      </div>
    </div>
  </Teleport>
</template>

<script setup>
import { ref, computed, watch, onUnmounted } from 'vue';

const props = defineProps({
  visible: Boolean,
  channels: { type: Array, required: true },
  t: { type: Object, default: () => ({}) }
});

const emit = defineEmits(['close', 'calibrate']);

const steps = [
  { key: 'detect', label: '选择摇杆' },
  { key: 'center', label: '确认中位' },
  { key: 'rotate', label: '旋转三圈' },
  { key: 'complete', label: '完成' }
];

const step = ref('detect');
const stepIndex = computed(() => steps.findIndex(s => s.key === step.value));
const detected = ref(false);
const selectedStick = ref(null); // 'left' or 'right'
const rawCaptures = ref({ minX: 2048, maxX: 2048, minY: 2048, maxY: 2048 });
const capturedCenter = ref({ x: -1, y: -1 });
const baseline = ref({ left: { x: 2048, y: 2048 }, right: { x: 2048, y: 2048 } });

const t = computed(() => props.t);

// 显示用映射值（与仪表盘摇杆视图一致），采集校准数据才用 ADC 原始值
// 含圆形约束，与 Joystick 组件一致
function constrainCircle(x, y, max) {
  const d = Math.sqrt(x * x + y * y);
  if (d > max) return { x: x / d * max, y: y / d * max };
  return { x, y };
}

const displayPos = computed(() => {
  const ch = props.channels; if (!ch || ch.length < 4 || !selectedStick.value) return { x: 0, y: 0 };
  const rx = selectedStick.value === 'left' ? ch[3].rawValue : ch[0].rawValue;
  const ry = selectedStick.value === 'left' ? ch[2].rawValue : ch[1].rawValue;
  const cx = capturedCenter.value.x, cy = capturedCenter.value.y;
  let dx, dy;
  if (cx !== -1) {
    dx = -((rx - cx) / 2048) * 86;
    dy = -((ry - cy) / 2048) * 86;
  } else {
    const mvx = selectedStick.value === 'left' ? ch[3].mappedValue : ch[0].mappedValue;
    const mvy = selectedStick.value === 'left' ? ch[2].mappedValue : ch[1].mappedValue;
    dx = ((mvx - 1500) / 500) * 86;
    dy = -((mvy - 1500) / 500) * 86;
  }
  return constrainCircle(dx, dy, 86);
});

const instruction = computed(() => {
  const texts = t.value;
  switch (step.value) {
    case 'detect': return texts.calDetect || '持续轻推要校准的摇杆';
    case 'center': return texts.calCenter || '将摇杆置于中位，点击下一步';
    case 'rotate': return texts.calRotate || '以最大角度转动摇杆数圈后点击下一步';
    case 'complete': return texts.calComplete || '校准完成！';
    default: return '';
  }
});

const hint = computed(() => {
  const texts = t.value;
  switch (step.value) {
    case 'detect': return texts.calDetectHint || '';
    case 'center': return '';
    case 'rotate': return texts.calRotateHint || '转动过程中会自动记录各方向极值';
    case 'complete': return '';
    default: return '';
  }
});

const calibratedAxes = computed(() => {
  const cap = rawCaptures.value;
  if (selectedStick.value === 'left') {
    return [
      { label: t.value.calAxisLeftY || '左 Y (油门)', min: cap.minY, max: cap.maxY },
      { label: t.value.calAxisLeftX || '左 X (偏航)', min: cap.minX, max: cap.maxX }
    ];
  }
  return [
    { label: t.value.calAxisRightX || '右 X (横滚)', min: cap.minX, max: cap.maxX },
    { label: t.value.calAxisRightY || '右 Y (俯仰)', min: cap.minY, max: cap.maxY }
  ];
});

let detectTimer = null;
let rotateTimer = null;

function reset() {
  step.value = 'detect';
  detected.value = false;
  selectedStick.value = null;
  rawCaptures.value = { minX: 2048, maxX: 2048, minY: 2048, maxY: 2048 };
  capturedCenter.value = { x: -1, y: -1 };
  baseline.value = { left: { x: 2048, y: 2048 }, right: { x: 2048, y: 2048 } };
  if (detectTimer) clearTimeout(detectTimer);
  if (rotateTimer) clearTimeout(rotateTimer);
}

function onCancel() {
  reset();
  emit('close');
}

function onNextCenter() {
  const ch = props.channels;
  if (!ch || ch.length < 4) return;
  if (selectedStick.value === 'left') {
    capturedCenter.value = { x: ch[3].rawValue, y: ch[2].rawValue };
  } else {
    capturedCenter.value = { x: ch[0].rawValue, y: ch[1].rawValue };
  }
  step.value = 'rotate';
}

function onNextRotate() {
  step.value = 'complete';
}

function onConfirm() {
  const cap = rawCaptures.value;
  const mid = capturedCenter.value;
  const calResults = {};
  if (selectedStick.value === 'left') {
    calResults[3] = { min: Math.min(cap.minX, cap.maxX), mid: mid.x, max: Math.max(cap.minX, cap.maxX) };
    calResults[2] = { min: Math.min(cap.minY, cap.maxY), mid: mid.y, max: Math.max(cap.minY, cap.maxY) };
  } else {
    calResults[0] = { min: Math.min(cap.minX, cap.maxX), mid: mid.x, max: Math.max(cap.minX, cap.maxX) };
    calResults[1] = { min: Math.min(cap.minY, cap.maxY), mid: mid.y, max: Math.max(cap.minY, cap.maxY) };
  }
  reset();
  emit('calibrate', calResults);
  emit('close');
}

// Detection + 原始 ADC 极值捕获
watch([() => props.channels, () => props.visible], ([ch]) => {
  if (!props.visible || !ch || ch.length < 4) return;

  if (step.value === 'detect' && !detected.value) {
    // 首次进入 detect，记录静止位置作为基准
    if (!detected.value && baseline.value.left.x === 2048) {
      baseline.value = {
        left:  { x: ch[3].rawValue, y: ch[2].rawValue },
        right: { x: ch[0].rawValue, y: ch[1].rawValue }
      };
    }
    const lb = baseline.value.left, rb = baseline.value.right;
    const leftMove  = Math.abs(ch[2].rawValue - lb.y) + Math.abs(ch[3].rawValue - lb.x);
    const rightMove = Math.abs(ch[0].rawValue - rb.x) + Math.abs(ch[1].rawValue - rb.y);
    if (leftMove > 80 || rightMove > 80) {
      if (detectTimer) clearTimeout(detectTimer);
      detected.value = true;
      selectedStick.value = leftMove > rightMove ? 'left' : 'right';
      step.value = 'center';
    }
  }

  if (!selectedStick.value) return;

  // 在 rotate 阶段持续捕获原始 ADC 极值
  if (step.value === 'rotate') {
    const rx = selectedStick.value === 'left' ? ch[3].rawValue : ch[0].rawValue;
    const ry = selectedStick.value === 'left' ? ch[2].rawValue : ch[1].rawValue;
    const cap = rawCaptures.value;
    cap.minX = Math.min(cap.minX, rx);
    cap.maxX = Math.max(cap.maxX, rx);
    cap.minY = Math.min(cap.minY, ry);
    cap.maxY = Math.max(cap.maxY, ry);
  }
}, { deep: true });

onUnmounted(() => {
  if (detectTimer) clearTimeout(detectTimer);
  if (rotateTimer) clearTimeout(rotateTimer);
});
</script>
