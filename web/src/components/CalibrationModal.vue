<template>
  <Teleport to="body">
    <div v-if="visible" class="fixed inset-0 z-50 flex items-center justify-center bg-black/70" @click.self="onCancel">
      <div class="bg-[#1a1a1a] border border-white/10 rounded-3xl p-8 w-[420px] select-none">
        <!-- Step Progress -->
        <div class="flex items-center gap-2 mb-6">
          <div v-for="(s, i) in steps" :key="i" class="flex items-center gap-2">
            <div :class="['w-7 h-7 rounded-full flex items-center justify-center text-xs font-bold transition-colors', i <= stepIndex ? 'bg-darwin-amber text-black' : 'bg-white/10 text-darwin-muted']">{{ i + 1 }}</div>
            <div v-if="i < steps.length - 1" :class="['w-8 h-px transition-colors', i < stepIndex ? 'bg-darwin-amber' : 'bg-white/10']"></div>
          </div>
        </div>

        <!-- Joystick Visual -->
        <div class="relative w-[240px] h-[240px] mx-auto mb-6">
          <svg viewBox="0 0 240 240" class="w-full h-full">
            <!-- Outer boundary -->
            <circle cx="120" cy="120" r="100" fill="none" stroke="rgba(255,255,255,0.12)" stroke-width="2"/>
            <circle cx="120" cy="120" r="68" fill="none" stroke="rgba(255,255,255,0.05)" stroke-width="1"/>
            <!-- Crosshair -->
            <line x1="120" y1="20" x2="120" y2="220" stroke="rgba(255,255,255,0.06)" stroke-width="1"/>
            <line x1="20" y1="120" x2="220" y2="120" stroke="rgba(255,255,255,0.06)" stroke-width="1"/>
            <!-- Stick dot -->
            <circle :cx="120 + displayX" :cy="120 + displayY" r="14" fill="#F5A623" opacity="0.9" filter="url(#glow)"/>
            <defs>
              <filter id="glow">
                <feGaussianBlur stdDeviation="3" result="blur"/>
                <feMerge><feMergeNode in="blur"/><feMergeNode in="SourceGraphic"/></feMerge>
              </filter>
            </defs>
            <!-- Direction arrows -->
            <template v-if="step === 'push_max'">
              <polygon points="120,14 114,26 126,26" fill="#F5A623" opacity="0.8"/>
              <text x="120" y="8" text-anchor="middle" fill="#F5A623" font-size="10" opacity="0.6">MAX</text>
            </template>
            <template v-if="step === 'rotate'">
              <path d="M 200,120 A 80,80 0 1,1 180,50" fill="none" stroke="#F5A623" stroke-width="2.5" stroke-dasharray="6,4" opacity="0.6"/>
              <polygon points="176,44 168,52 184,52" fill="#F5A623" opacity="0.6"/>
            </template>
          </svg>
          <!-- Step-specific overlays -->
          <div v-if="step === 'detect' && !detected" class="absolute inset-0 flex items-center justify-center">
            <div class="text-darwin-amber text-4xl animate-pulse">⟳</div>
          </div>
        </div>

        <!-- Instruction -->
        <p class="text-center text-white text-base mb-1 font-medium">{{ instruction }}</p>
        <p v-if="hint" class="text-center text-darwin-muted text-sm mb-6">{{ hint }}</p>
        <p v-else class="mb-6"></p>

        <!-- Completion Stats -->
        <div v-if="step === 'complete'" class="grid grid-cols-2 gap-3 mb-6">
          <div v-for="(axis, i) in calibratedAxes" :key="i" class="p-3 border border-white/10 rounded-xl bg-white/5">
            <span class="text-darwin-muted text-xs block mb-1">{{ axis.label }}</span>
            <span class="text-white text-sm">min {{ axis.min }} · max {{ axis.max }}</span>
          </div>
        </div>

        <!-- Buttons -->
        <div class="flex justify-center gap-4">
          <button v-if="step !== 'complete'" @click="onCancel" class="px-6 py-2.5 rounded-full border border-white/10 text-darwin-ink bg-white/5 hover:bg-white/10">取消</button>
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
  { key: 'push_max', label: '推到上限' },
  { key: 'rotate', label: '旋转三圈' },
  { key: 'complete', label: '完成' }
];

const step = ref('detect');
const stepIndex = computed(() => steps.findIndex(s => s.key === step.value));
const detected = ref(false);
const selectedStick = ref(null); // 'left' or 'right'
const rawCaptures = ref({ minX: 2048, maxX: 2048, minY: 2048, maxY: 2048 });
const rotationCount = ref(0);
const prevAngle = ref(null);
const unwrappedAngle = ref(0);
const totalRotation = ref(0);

const t = computed(() => props.t);

function getStickData() {
  const ch = props.channels;
  if (!ch || ch.length < 4) return null;
  if (selectedStick.value === 'left') {
    return { x: ch[3].rawValue, y: ch[2].rawValue }; // CH4=Yaw, CH3=Throttle
  }
  return { x: ch[0].rawValue, y: ch[1].rawValue }; // CH1=Roll, CH2=Pitch
}

const stickData = computed(() => getStickData());

const displayX = computed(() => {
  if (!stickData.value) return 0;
  return ((stickData.value.x - 2048) / 2048) * 86;
});

const displayY = computed(() => {
  if (!stickData.value) return 0;
  return ((stickData.value.y - 2048) / 2048) * 86;
});

const instruction = computed(() => {
  const texts = t.value;
  switch (step.value) {
    case 'detect': return texts.calDetect || '持续轻推要校准的摇杆';
    case 'push_max': return texts.calPushMax || '将摇杆推到上方最大值处';
    case 'rotate': return texts.calRotate || '以最大角度顺时针转动摇杆 3 圈';
    case 'complete': return texts.calComplete || '校准完成！';
    default: return '';
  }
});

const hint = computed(() => {
  const texts = t.value;
  switch (step.value) {
    case 'detect': return texts.calDetectHint || '';
    case 'push_max': return texts.calPushMaxHint || '到达上限后松手，将进入下一步';
    case 'rotate': {
      const count = Math.min(rotationCount.value, 3);
      return `${texts.calRotateHint || '已转动'}：${count}/3 ${texts.calRotateUnit || '圈'}`;
    }
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
  rotationCount.value = 0;
  prevAngle.value = null;
  unwrappedAngle.value = 0;
  totalRotation.value = 0;
  if (detectTimer) clearTimeout(detectTimer);
  if (rotateTimer) clearTimeout(rotateTimer);
}

function onCancel() {
  reset();
  emit('close');
}

function onConfirm() {
  const cap = rawCaptures.value;
  const calResults = {};
  if (selectedStick.value === 'left') {
    const midX = props.channels[3]?.rawValue ?? 2048;
    const midY = props.channels[2]?.rawValue ?? 2048;
    calResults[3] = { min: Math.min(cap.minX, cap.maxX), mid: midX, max: Math.max(cap.minX, cap.maxX) };
    calResults[4] = { min: Math.min(cap.minY, cap.maxY), mid: midY, max: Math.max(cap.minY, cap.maxY) };
  } else {
    const midX = props.channels[0]?.rawValue ?? 2048;
    const midY = props.channels[1]?.rawValue ?? 2048;
    calResults[0] = { min: Math.min(cap.minX, cap.maxX), mid: midX, max: Math.max(cap.minX, cap.maxX) };
    calResults[1] = { min: Math.min(cap.minY, cap.maxY), mid: midY, max: Math.max(cap.minY, cap.maxY) };
  }
  reset();
  emit('calibrate', calResults);
}

// Detection watcher
watch([() => props.channels, () => props.visible], () => {
  if (!props.visible) {
    reset();
    return;
  }
  if (props.visible && step.value === 'detect') {
    detectTimer = setTimeout(() => {
      if (step.value === 'detect') {
        selectedStick.value = 'right';
        detected.value = true;
        step.value = 'push_max';
      }
    }, 8000);
  }
}, { immediate: true });

watch(stickData, (data) => {
  if (!data || !props.visible) return;

  if (step.value === 'detect' && !detected.value) {
    const dx = Math.abs(data.x - 2048);
    const dy = Math.abs(data.y - 2048);
    if (dx > 80 || dy > 80) {
      if (detectTimer) clearTimeout(detectTimer);
      detected.value = true;
      // Determine which stick based on CH movement
      const ch = props.channels;
      const rightDist = Math.abs(ch[0].rawValue - 2048) + Math.abs(ch[1].rawValue - 2048);
      const leftDist = Math.abs(ch[2].rawValue - 2048) + Math.abs(ch[3].rawValue - 2048);
      selectedStick.value = leftDist > rightDist ? 'left' : 'right';
      step.value = 'push_max';
    }
  }

  if (step.value === 'push_max') {
    const cap = rawCaptures.value;
    cap.minX = Math.min(cap.minX, data.x);
    cap.maxX = Math.max(cap.maxX, data.x);
    cap.minY = Math.min(cap.minY, data.y);
    cap.maxY = Math.max(cap.maxY, data.y);
    // Detect release (back to center) to advance
    const dist = Math.sqrt((data.x - 2048) ** 2 + (data.y - 2048) ** 2);
    if (dist < 30 && (cap.maxY > 2150 || cap.minY < 1950)) {
      step.value = 'rotate';
    }
  }

  if (step.value === 'rotate') {
    const cap = rawCaptures.value;
    cap.minX = Math.min(cap.minX, data.x);
    cap.maxX = Math.max(cap.maxX, data.x);
    cap.minY = Math.min(cap.minY, data.y);
    cap.maxY = Math.max(cap.maxY, data.y);

    const angle = Math.atan2(data.y - 2048, data.x - 2048);
    if (prevAngle.value !== null) {
      let delta = angle - prevAngle.value;
      if (delta > Math.PI) delta -= 2 * Math.PI;
      if (delta < -Math.PI) delta += 2 * Math.PI;
      unwrappedAngle.value += delta;
      totalRotation.value = Math.abs(unwrappedAngle.value) / (2 * Math.PI);
      rotationCount.value = Math.floor(totalRotation.value);
      if (rotationCount.value >= 3) {
        step.value = 'complete';
      }
    }
    prevAngle.value = angle;
  }
}, { deep: true });

onUnmounted(() => {
  if (detectTimer) clearTimeout(detectTimer);
  if (rotateTimer) clearTimeout(rotateTimer);
});
</script>
