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
    <div class="flex flex-col gap-3">
      <AuxChannelSlider
        v-for="ch in channels"
        :key="ch.id"
        :channel-id="ch.id"
        :mapped-value="ch.mappedValue"
        v-model:raw-value="ch.rawValue"
        :cal="ch.cal"
        :epa="epaForChannel(ch.id)"
        :rev-on="revForChannel(ch.id)"
        :t-raw-label="tRawLabel"
        @update-epa="(chId, pos, neg) => $emit('updateEpa', chId, pos, neg)"
        @toggle-rev="(chId) => $emit('toggleRev', chId)"
      />
    </div>
  </div>
</template>

<script setup>
import AuxChannelSlider from './AuxChannelSlider.vue'

const props = defineProps({
  channels: { type: Array, required: true },
  epaData: { type: Array, default: () => [] },
  revMask: { type: Number, default: 0 },
  t: { type: Object, required: true },
  tRawLabel: { type: String, default: 'RAW' }
})

defineEmits(['calibrate', 'save', 'updateEpa', 'toggleRev'])

function epaForChannel(ch) {
  const e = props.epaData.find(e => e.ch === ch)
  return e || { pos: 100, neg: 100 }
}

function revForChannel(ch) {
  return (props.revMask & (1 << (ch - 1))) !== 0
}
</script>
