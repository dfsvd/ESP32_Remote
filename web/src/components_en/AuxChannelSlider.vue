<template>
  <div class="w-full box-border p-3 lg:p-[11px_15px] border border-[var(--theme-border-light)] rounded-[18px] bg-darwin-panel shadow-[0_14px_28px_var(--theme-shadow-sm)]">
    <!-- Desktop: single row. Mobile: row1=CH+slider+%, row2=REV+EPA -->
    <div class="flex flex-col gap-1.5 lg:gap-0">
      <div class="flex items-center gap-2 lg:gap-3">
        <!-- CH Label -->
        <label class="shrink-0 font-bold text-[0.85rem] lg:text-[0.95rem] text-darwin-muted">CH{{ channelId }}</label>

        <!-- REV + EPA (desktop: inline here; mobile: hidden, shown in row2) -->
        <div class="hidden lg:flex items-center gap-2 lg:gap-3">
          <button
            type="button"
            @click="$emit('toggleRev', channelId)"
            :class="['shrink-0 px-2 py-0.5 text-[0.6rem] lg:text-[0.7rem] font-bold rounded-full border transition-all',
              revOn ? 'bg-red-500/20 text-red-400 border-red-500/30' : 'bg-[var(--theme-bg-active)] text-darwin-muted border-[var(--theme-border)]']"
          >
            {{ revOn ? 'REV' : 'NOR' }}
          </button>

          <span class="text-[0.6rem] lg:text-[0.65rem] text-darwin-muted">Pos</span>
          <input
            type="number" min="0" max="200" :value="epa.pos"
            @change="onEpaPos($event)"
            class="w-12 lg:w-14 p-0.5 text-[0.65rem] text-center bg-[var(--theme-input-bg)] border border-[var(--theme-input-border)] rounded text-[var(--theme-input-text)] [appearance:textfield] [&::-webkit-inner-spin-button]:appearance-none [&::-webkit-outer-spin-button]:appearance-none"
          />

          <span class="text-[0.6rem] lg:text-[0.65rem] text-darwin-muted">Neg</span>
          <input
            type="number" min="0" max="200" :value="epa.neg"
            @change="onEpaNeg($event)"
            class="w-12 lg:w-14 p-0.5 text-[0.65rem] text-center bg-[var(--theme-input-bg)] border border-[var(--theme-input-border)] rounded text-[var(--theme-input-text)] [appearance:textfield] [&::-webkit-inner-spin-button]:appearance-none [&::-webkit-outer-spin-button]:appearance-none"
          />
        </div>

        <!-- Channel slider -->
        <div class="flex-grow flex items-center min-w-0">
          <input
            type="range" min="-100" max="100" :value="percentValue" disabled
            class="w-full h-2.5 m-0 outline-none appearance-none rounded-[5px] border border-[var(--theme-input-border)] bg-gradient-to-r from-[var(--theme-bg-hover)] to-darwin-amber/20 [&::-webkit-slider-thumb]:appearance-none [&::-webkit-slider-thumb]:w-5 [&::-webkit-slider-thumb]:h-5 [&::-webkit-slider-thumb]:rounded-full [&::-webkit-slider-thumb]:bg-darwin-amber [&::-webkit-slider-thumb]:cursor-pointer [&::-webkit-slider-thumb]:shadow-[0_0_8px_rgba(245,166,35,0.7)] [&::-moz-range-thumb]:w-5 [&::-moz-range-thumb]:h-5 [&::-moz-range-thumb]:border-none [&::-moz-range-thumb]:rounded-full [&::-moz-range-thumb]:bg-darwin-amber [&::-moz-range-thumb]:cursor-pointer [&::-moz-range-thumb]:shadow-[0_0_8px_rgba(245,166,35,0.7)]"
          />
        </div>

        <span class="hidden lg:inline w-10 lg:w-12 shrink-0 text-right font-mono text-[0.8rem] lg:text-[0.9rem] text-darwin-ink">{{ percentValue }}%</span>

        <button
          class="hidden lg:block bg-transparent border-none px-0.5 text-[1.1rem] text-darwin-muted cursor-pointer shrink-0"
          @click="isCollapsed = !isCollapsed"
        >
          {{ isCollapsed ? '▶' : '▼' }}
        </button>
      </div>

      <!-- Row 2 (mobile only): REV + EPA + % + ▼, spread full width -->
      <div class="flex lg:hidden items-center gap-2">
        <button
          type="button"
          @click="$emit('toggleRev', channelId)"
          :class="['shrink-0 px-2.5 py-1 text-[0.65rem] font-bold rounded-full border transition-all',
            revOn ? 'bg-red-500/20 text-red-400 border-red-500/30' : 'bg-[var(--theme-bg-active)] text-darwin-muted border-[var(--theme-border)]']"
        >
          {{ revOn ? 'REV' : 'NOR' }}
        </button>

        <div class="flex-1 flex items-center gap-1 justify-end">
          <span class="text-[0.65rem] text-darwin-muted">Pos</span>
          <input
            type="number" min="0" max="200" :value="epa.pos"
            @change="onEpaPos($event)"
            class="w-14 p-0.5 text-[0.7rem] text-center bg-[var(--theme-input-bg)] border border-[var(--theme-input-border)] rounded text-[var(--theme-input-text)] [appearance:textfield] [&::-webkit-inner-spin-button]:appearance-none [&::-webkit-outer-spin-button]:appearance-none"
          />
        </div>

        <div class="flex-1 flex items-center gap-1 justify-end">
          <span class="text-[0.65rem] text-darwin-muted">Neg</span>
          <input
            type="number" min="0" max="200" :value="epa.neg"
            @change="onEpaNeg($event)"
            class="w-14 p-0.5 text-[0.7rem] text-center bg-[var(--theme-input-bg)] border border-[var(--theme-input-border)] rounded text-[var(--theme-input-text)] [appearance:textfield] [&::-webkit-inner-spin-button]:appearance-none [&::-webkit-outer-spin-button]:appearance-none"
          />
        </div>

        <span class="shrink-0 font-mono text-[0.8rem] text-darwin-ink">{{ percentValue }}%</span>

        <button
          class="bg-transparent border-none px-0.5 text-[1.1rem] text-darwin-muted cursor-pointer shrink-0"
          @click="isCollapsed = !isCollapsed"
        >
          {{ isCollapsed ? '▶' : '▼' }}
        </button>
      </div>
    </div>

    <!-- Calibration Details -->
    <div v-if="!isCollapsed" class="mt-3 lg:mt-4 pt-3 lg:pt-4 border-t border-[var(--theme-input-border)] flex flex-col gap-3">
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
          class="block w-full min-w-0 h-2 appearance-none outline-none rounded bg-gradient-to-r from-[var(--theme-bg-hover)] to-darwin-amber/20 transition-opacity duration-200 [&::-webkit-slider-thumb]:appearance-none [&::-webkit-slider-thumb]:w-[18px] [&::-webkit-slider-thumb]:h-[18px] [&::-webkit-slider-thumb]:rounded-full [&::-webkit-slider-thumb]:bg-[var(--theme-input-text)] [&::-webkit-slider-thumb]:border-2 [&::-webkit-slider-thumb]:border-darwin-amber [&::-webkit-slider-thumb]:cursor-pointer [&::-moz-range-thumb]:w-[18px] [&::-moz-range-thumb]:h-[18px] [&::-moz-range-thumb]:rounded-full [&::-moz-range-thumb]:bg-[var(--theme-input-text)] [&::-moz-range-thumb]:border-2 [&::-moz-range-thumb]:border-darwin-amber [&::-moz-range-thumb]:cursor-pointer"
        />
      </div>

      <!-- Min/Mid/Max Config -->
      <div class="grid grid-cols-[42px_minmax(0,1fr)_72px] items-center gap-3">
        <label>Min:</label>
        <input type="number" v-model.number="cal.min" :disabled="!captureMin"
          class="w-full min-w-0 p-1 bg-[var(--theme-input-bg)] border border-[var(--theme-input-border)] rounded-[10px] text-[var(--theme-input-text)] disabled:opacity-35" />
        <div class="relative shrink-0 w-[36px] h-[20px] rounded-[10px] cursor-pointer transition-colors duration-200 bg-[var(--theme-bg-active)]"
          :class="{ '!bg-[#00e676]': captureMin }" @click="toggleCaptureMin">
          <div class="absolute top-[2px] left-[2px] w-[16px] h-[16px] rounded-full bg-white transition-all duration-200" :class="{ 'left-[18px]': captureMin }"></div>
        </div>
      </div>

      <div class="grid grid-cols-[42px_minmax(0,1fr)_72px] items-center gap-3">
        <label>Mid:</label>
        <input type="number" v-model.number="cal.mid" :disabled="!captureMid"
          class="w-full min-w-0 p-1 bg-[var(--theme-input-bg)] border border-[var(--theme-input-border)] rounded-[10px] text-[var(--theme-input-text)] disabled:opacity-35" />
        <div class="relative shrink-0 w-[36px] h-[20px] rounded-[10px] cursor-pointer transition-colors duration-200 bg-[var(--theme-bg-active)]"
          :class="{ '!bg-[#00e676]': captureMid }" @click="toggleCaptureMid">
          <div class="absolute top-[2px] left-[2px] w-[16px] h-[16px] rounded-full bg-white transition-all duration-200" :class="{ 'left-[18px]': captureMid }"></div>
        </div>
      </div>

      <div class="grid grid-cols-[42px_minmax(0,1fr)_72px] items-center gap-3">
        <label>Max:</label>
        <input type="number" v-model.number="cal.max" :disabled="!captureMax"
          class="w-full min-w-0 p-1 bg-[var(--theme-input-bg)] border border-[var(--theme-input-border)] rounded-[10px] text-[var(--theme-input-text)] disabled:opacity-35" />
        <div class="relative shrink-0 w-[36px] h-[20px] rounded-[10px] cursor-pointer transition-colors duration-200 bg-[var(--theme-bg-active)]"
          :class="{ '!bg-[#00e676]': captureMax }" @click="toggleCaptureMax">
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
  epa: { type: Object, default: () => ({ pos: 100, neg: 100 }) },
  revOn: { type: Boolean, default: false },
  tRawLabel: { type: String, default: 'RAW' }
});

const emit = defineEmits(['update:rawValue', 'updateEpa', 'toggleRev']);

function onEpaPos(e) {
  const val = Math.max(0, Math.min(200, parseInt(e.target.value, 10) || 100))
  emit('updateEpa', props.channelId, val, props.epa.neg)
}

function onEpaNeg(e) {
  const val = Math.max(0, Math.min(200, parseInt(e.target.value, 10) || 100))
  emit('updateEpa', props.channelId, props.epa.pos, val)
}

const isCollapsed = ref(true);

const percentValue = computed(() => Math.round((props.mappedValue - 1500) / 5));

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
