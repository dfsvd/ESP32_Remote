<template>
  <div class="w-full box-border p-3 lg:p-[11px_15px] border border-white/5 rounded-[18px] bg-darwin-panel shadow-[0_14px_28px_rgba(24,33,38,0.06)]">
    <!-- Channel Header -->
    <div class="flex items-center gap-[13px] lg:gap-[14px]">
      <label class="w-[42px] lg:w-[50px] font-bold text-[0.92rem] lg:text-base text-darwin-muted">CH{{ channelId }}</label>
      <div class="flex-grow flex items-center">
        <input 
          type="range" 
          min="-100" 
          max="100" 
          :value="percentValue" 
          disabled 
          class="w-full h-2.5 m-0 outline-none appearance-none rounded-[5px] border border-[#444] bg-gradient-to-r from-white/10 to-darwin-amber/20 [&::-webkit-slider-thumb]:appearance-none [&::-webkit-slider-thumb]:w-5 [&::-webkit-slider-thumb]:h-5 [&::-webkit-slider-thumb]:rounded-full [&::-webkit-slider-thumb]:bg-darwin-amber [&::-webkit-slider-thumb]:cursor-pointer [&::-webkit-slider-thumb]:shadow-[0_0_8px_rgba(245,166,35,0.7)] [&::-moz-range-thumb]:w-5 [&::-moz-range-thumb]:h-5 [&::-moz-range-thumb]:border-none [&::-moz-range-thumb]:rounded-full [&::-moz-range-thumb]:bg-darwin-amber [&::-moz-range-thumb]:cursor-pointer [&::-moz-range-thumb]:shadow-[0_0_8px_rgba(245,166,35,0.7)]"
        />
      </div>
      <span class="w-[44px] lg:w-[50px] text-right font-mono text-[0.88rem] lg:text-base text-darwin-ink">{{ percentValue }}%</span>
      <button 
        class="bg-transparent border-none px-1 lg:px-2 text-[1.2rem] text-darwin-muted cursor-pointer"
        @click="isCollapsed = !isCollapsed"
      >
        {{ isCollapsed ? '▶' : '▼' }}
      </button>
    </div>

    <!-- Calibration Details -->
    <div v-if="!isCollapsed" class="mt-[0.8rem] lg:mt-4 pt-[0.8rem] lg:pt-4 border-t border-[#444] flex flex-col gap-3">
      <div class="text-center font-mono text-[0.82rem] lg:text-[0.9rem] text-darwin-muted">
        {{ tRawLabel }}: {{ rawValue }}
      </div>
      
      <div class="w-full min-w-0 py-1">
        <input 
          type="range" 
          min="0" 
          max="4095" 
          :value="rawValue" 
          @input="$emit('update:rawValue', parseInt($event.target.value, 10))"
          class="block w-full min-w-0 h-2 appearance-none outline-none rounded bg-gradient-to-r from-white/10 to-darwin-amber/20 transition-opacity duration-200 [&::-webkit-slider-thumb]:appearance-none [&::-webkit-slider-thumb]:w-[18px] [&::-webkit-slider-thumb]:h-[18px] [&::-webkit-slider-thumb]:rounded-full [&::-webkit-slider-thumb]:bg-[#f0f0f5] [&::-webkit-slider-thumb]:border-2 [&::-webkit-slider-thumb]:border-darwin-amber [&::-webkit-slider-thumb]:cursor-pointer [&::-moz-range-thumb]:w-[18px] [&::-moz-range-thumb]:h-[18px] [&::-moz-range-thumb]:rounded-full [&::-moz-range-thumb]:bg-[#f0f0f5] [&::-moz-range-thumb]:border-2 [&::-moz-range-thumb]:border-darwin-amber [&::-moz-range-thumb]:cursor-pointer"
        />
      </div>

      <!-- Min/Mid/Max Config -->
      <div class="grid grid-cols-[42px_minmax(0,1fr)_72px] lg:grid-cols-[40px_minmax(0,1fr)_72px] items-center gap-[0.65rem] lg:gap-3">
        <label>Min:</label>
        <input 
          type="number" 
          v-model.number="cal.min" 
          :disabled="!captureMin"
          class="w-full min-w-0 p-1 bg-[#333] border border-[#555] rounded-[10px] text-[#f0f0f5] text-[0.95rem] lg:text-base disabled:opacity-35"
        />
        <div 
          class="relative shrink-0 w-[36px] h-[20px] rounded-[10px] cursor-pointer transition-colors duration-200 bg-white/20"
          :class="{ '!bg-[#00e676]': captureMin }"
          @click="toggleCaptureMin"
        >
          <div class="absolute top-[2px] left-[2px] w-[16px] h-[16px] rounded-full bg-white transition-all duration-200" :class="{ 'left-[18px]': captureMin }"></div>
        </div>
      </div>

      <div class="grid grid-cols-[42px_minmax(0,1fr)_72px] lg:grid-cols-[40px_minmax(0,1fr)_72px] items-center gap-[0.65rem] lg:gap-3">
        <label>Mid:</label>
        <input 
          type="number" 
          v-model.number="cal.mid" 
          :disabled="!captureMid"
          class="w-full min-w-0 p-1 bg-[#333] border border-[#555] rounded-[10px] text-[#f0f0f5] text-[0.95rem] lg:text-base disabled:opacity-35"
        />
        <div 
          class="relative shrink-0 w-[36px] h-[20px] rounded-[10px] cursor-pointer transition-colors duration-200 bg-white/20"
          :class="{ '!bg-[#00e676]': captureMid }"
          @click="toggleCaptureMid"
        >
          <div class="absolute top-[2px] left-[2px] w-[16px] h-[16px] rounded-full bg-white transition-all duration-200" :class="{ 'left-[18px]': captureMid }"></div>
        </div>
      </div>

      <div class="grid grid-cols-[42px_minmax(0,1fr)_72px] lg:grid-cols-[40px_minmax(0,1fr)_72px] items-center gap-[0.65rem] lg:gap-3">
        <label>Max:</label>
        <input 
          type="number" 
          v-model.number="cal.max" 
          :disabled="!captureMax"
          class="w-full min-w-0 p-1 bg-[#333] border border-[#555] rounded-[10px] text-[#f0f0f5] text-[0.95rem] lg:text-base disabled:opacity-35"
        />
        <div 
          class="relative shrink-0 w-[36px] h-[20px] rounded-[10px] cursor-pointer transition-colors duration-200 bg-white/20"
          :class="{ '!bg-[#00e676]': captureMax }"
          @click="toggleCaptureMax"
        >
          <div class="absolute top-[2px] left-[2px] w-[16px] h-[16px] rounded-full bg-white transition-all duration-200" :class="{ 'left-[18px]': captureMax }"></div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch } from 'vue';

const props = defineProps({
  channelId: { type: Number, required: true },
  mappedValue: { type: Number, default: 1500 },
  rawValue: { type: Number, default: 2048 },
  cal: { type: Object, required: true },
  tRawLabel: { type: String, default: 'RAW' }
});

const emit = defineEmits(['update:rawValue']);

const isCollapsed = ref(true);

const percentValue = computed(() => {
  return Math.round((props.mappedValue - 1500) / 5);
});

const captureMin = ref(false);
const captureMid = ref(false);
const captureMax = ref(false);

watch(() => props.rawValue, (newVal) => {
  if (captureMin.value) props.cal.min = newVal;
  if (captureMid.value) props.cal.mid = newVal;
  if (captureMax.value) props.cal.max = newVal;
});

function toggleCaptureMin() {
  captureMin.value = !captureMin.value;
  if (captureMin.value) props.cal.min = props.rawValue;
}

function toggleCaptureMid() {
  captureMid.value = !captureMid.value;
  if (captureMid.value) props.cal.mid = props.rawValue;
}

function toggleCaptureMax() {
  captureMax.value = !captureMax.value;
  if (captureMax.value) props.cal.max = props.rawValue;
}
</script>
