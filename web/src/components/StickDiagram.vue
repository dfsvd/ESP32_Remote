<template>
  <div class="flex flex-col items-center gap-2">
    <svg width="130" height="150" viewBox="0 0 130 150" class="shrink-0">
      <!-- Stick base circle -->
      <circle cx="65" cy="75" r="50" fill="none" stroke="var(--theme-svg-stroke)" stroke-width="2"/>
      <circle cx="65" cy="75" r="33" fill="none" stroke="var(--theme-svg-stroke-subtle)" stroke-width="1"/>
      <!-- Crosshair -->
      <line x1="15" y1="75" x2="115" y2="75" stroke="var(--theme-svg-stroke-faint)" stroke-width="1"/>
      <line x1="65" y1="25" x2="65" y2="125" stroke="var(--theme-svg-stroke-faint)" stroke-width="1"/>
      <!-- Center dot -->
      <circle cx="65" cy="75" r="6" fill="#F5A623" opacity="0.6"/>

      <!-- X-axis arrows & labels -->
      <!-- Left arrow -->
      <polygon v-if="xPositive === 'left' || xPositive === 'both'"
        :points="leftArrowPoints" fill="#F5A623" opacity="0.8"/>
      <text x="10" y="80" text-anchor="middle" font-size="9" font-weight="bold"
        :fill="(xPositive === 'left' || xPositive === 'both') ? '#F5A623' : 'var(--theme-stick-inactive-text)'">
        {{ leftLabel }}
      </text>
      <!-- Right arrow -->
      <polygon v-if="xPositive === 'right' || xPositive === 'both'"
        :points="rightArrowPoints" fill="#F5A623" opacity="0.8"/>
      <text x="120" y="80" text-anchor="middle" font-size="9" font-weight="bold"
        :fill="(xPositive === 'right' || xPositive === 'both') ? '#F5A623' : 'var(--theme-stick-inactive-text)'">
        {{ rightLabel }}
      </text>

      <!-- Y-axis arrows & labels -->
      <!-- Top arrow -->
      <polygon v-if="yPositive === 'top' || yPositive === 'both'"
        :points="topArrowPoints" fill="#F5A623" opacity="0.8"/>
      <text x="65" y="18" text-anchor="middle" font-size="9" font-weight="bold"
        :fill="(yPositive === 'top' || yPositive === 'both') ? '#F5A623' : 'var(--theme-stick-inactive-text)'">
        {{ topLabel }}
      </text>
      <!-- Bottom arrow -->
      <polygon v-if="yPositive === 'bottom' || yPositive === 'both'"
        :points="bottomArrowPoints" fill="#F5A623" opacity="0.8"/>
      <text x="65" y="137" text-anchor="middle" font-size="9" font-weight="bold"
        :fill="(yPositive === 'bottom' || yPositive === 'both') ? '#F5A623' : 'var(--theme-stick-inactive-text)'">
        {{ bottomLabel }}
      </text>
    </svg>
    <span class="text-darwin-muted text-[0.6rem] font-bold uppercase tracking-widest">{{ label }}</span>
  </div>
</template>

<script setup>
import { computed } from 'vue'

const props = defineProps({
  label: String,
  leftLabel: { type: String, default: '' },
  rightLabel: { type: String, default: '' },
  topLabel: { type: String, default: '' },
  bottomLabel: { type: String, default: '' },
  xPositive: { type: String, default: 'both' },
  yPositive: { type: String, default: 'both' },
})

const as = 7 // arrow size
const cx = 65, cy = 75, r = 50

const leftArrowPoints = computed(() => {
  const x = cx - r + 6, y = cy
  return `${x},${y} ${x+as},${y-as} ${x+as},${y+as}`
})
const rightArrowPoints = computed(() => {
  const x = cx + r - 6, y = cy
  return `${x},${y} ${x-as},${y-as} ${x-as},${y+as}`
})
const topArrowPoints = computed(() => {
  const x = cx, y = cy - r + 6
  return `${x},${y} ${x-as},${y+as} ${x+as},${y+as}`
})
const bottomArrowPoints = computed(() => {
  const x = cx, y = cy + r - 6
  return `${x},${y} ${x-as},${y-as} ${x+as},${y-as}`
})
</script>
