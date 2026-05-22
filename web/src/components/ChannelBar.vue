<template>
  <div class="flex items-center gap-2 py-0.5">
    <span class="text-xs font-bold text-darwin-muted w-8 shrink-0 text-right">CH{{ channelId }}</span>
    <div class="flex-1 h-4 rounded-full bg-[var(--theme-bg-hover)] relative overflow-hidden border border-[var(--theme-border-light)]">
      <!-- center mark -->
      <div class="absolute top-0 bottom-0 left-1/2 w-px bg-darwin-muted/30 z-10"></div>
      <!-- fill bar: from center to value -->
      <div
        class="absolute top-0 bottom-0 rounded-full transition-all duration-150"
        :class="barClass"
        :style="barStyle"
      ></div>
    </div>
    <span class="text-xs font-mono text-[var(--theme-text)] w-10 text-right">{{ mappedValue }}</span>
    <span class="text-[0.6rem] font-mono w-10 text-right" :class="pctClass">{{ percentLabel }}</span>
  </div>
</template>

<script setup>
import { computed } from 'vue'

const props = defineProps({
  channelId: { type: Number, required: true },
  mappedValue: { type: Number, default: 1500 }
})

const percentLabel = computed(() => {
  const pct = Math.round((props.mappedValue - 1500) / 5)
  return (pct > 0 ? '+' : '') + pct + '%'
})

const pctClass = computed(() => {
  const pct = Math.round((props.mappedValue - 1500) / 5)
  if (pct > 5) return 'text-green-400'
  if (pct < -5) return 'text-red-400'
  return 'text-darwin-muted'
})

const barClass = computed(() => {
  const pct = Math.round((props.mappedValue - 1500) / 5)
  if (pct > 3) return 'from-50% to-right bg-green-500/60'
  if (pct < -3) return 'from-50% to-left bg-red-500/60'
  return 'bg-darwin-amber/50'
})

const barStyle = computed(() => {
  // position from center (50%) to value position (0% = 1000, 100% = 2000)
  const posPct = ((props.mappedValue - 1000) / 1000) * 100
  const clamped = Math.max(0, Math.min(100, posPct))
  if (clamped >= 50) {
    // bar goes from 50% to clamped%
    return { left: '50%', width: (clamped - 50) + '%' }
  } else {
    // bar goes from clamped% to 50%
    return { left: clamped + '%', width: (50 - clamped) + '%' }
  }
})
</script>
