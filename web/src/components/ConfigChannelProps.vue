<template>
  <div class="grid gap-4">
    <!-- Actions -->
    <div class="flex flex-wrap items-center gap-3">
      <button
        type="button"
        @click="$emit('calibrate')"
        class="px-4 py-2 text-sm font-bold rounded-full bg-gradient-to-br from-darwin-amber to-darwin-orange text-black"
      >
        {{ t.autoCalibration }}
      </button>
      <button
        type="button"
        @click="$emit('save')"
        class="px-4 py-2 text-sm font-bold rounded-full bg-darwin-amber/20 text-darwin-amber border border-darwin-amber/30"
      >
        {{ t.saveCalibration }}
      </button>
    </div>

    <!-- Channel Sliders -->
    <div class="grid grid-cols-1 lg:grid-cols-2 gap-3">
      <AuxChannelSlider
        v-for="ch in channels"
        :key="ch.id"
        :channel-id="ch.id"
        :mapped-value="ch.mappedValue"
        v-model:raw-value="ch.rawValue"
        :cal="ch.cal"
        :t-raw-label="tRawLabel"
      />
    </div>
  </div>
</template>

<script setup>
import AuxChannelSlider from './AuxChannelSlider.vue'

defineProps({
  channels: { type: Array, required: true },
  t: { type: Object, required: true },
  tRawLabel: { type: String, default: 'RAW' }
})

defineEmits(['calibrate', 'save'])
</script>
