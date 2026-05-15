<template>
  <div class="grid gap-5">
    <!-- Mode Selection -->
    <div class="flex flex-wrap items-center gap-3 p-4 rounded-2xl border border-white/10 bg-darwin-panel">
      <span class="text-sm text-darwin-muted font-bold">{{ t.stickMode }}:</span>
      <button type="button" @click="$emit('update:stickMode', 2)"
        :class="['px-4 py-2 text-sm font-bold rounded-full transition-all', stickMode === 2 ? 'bg-darwin-amber/20 text-darwin-amber border border-darwin-amber/30' : 'text-darwin-muted border border-white/10']"
      >{{ t.mode2 }}</button>
      <button type="button" @click="$emit('update:stickMode', 1)"
        :class="['px-4 py-2 text-sm font-bold rounded-full transition-all', stickMode === 1 ? 'bg-darwin-amber/20 text-darwin-amber border border-darwin-amber/30' : 'text-darwin-muted border border-white/10']"
      >{{ t.mode1 }}</button>
      <div class="flex-1"></div>
      <button type="button" @click="$emit('calibrate')"
        class="px-4 py-2 text-sm font-bold rounded-full bg-gradient-to-br from-darwin-amber to-darwin-orange text-black"
      >{{ t.calibrate }}</button>
    </div>

    <!-- Both Stick Modes Display -->
    <div class="grid grid-cols-1 lg:grid-cols-2 gap-4">

      <!-- Mode 2 (美国手 / American) -->
      <article :class="['p-5 rounded-2xl border transition-all', stickMode === 2 ? 'border-darwin-amber/40 bg-darwin-panel' : 'border-white/10 bg-darwin-panel/60']">
        <div class="flex items-center gap-2 mb-3">
          <div class="w-2 h-2 rounded-full" :class="stickMode === 2 ? 'bg-darwin-amber' : 'bg-white/20'"></div>
          <h4 class="m-0 text-darwin-amber text-[0.7rem] font-bold uppercase tracking-widest">{{ t.mode2 }}</h4>
        </div>
        <p class="text-darwin-muted text-[0.65rem] mb-4">{{ t.mode2Desc }}</p>
        <div class="flex justify-center gap-6 lg:gap-10">
          <StickDiagram :label="t.leftStick"
            left-label="Yaw" right-label="Yaw" x-positive="both"
            top-label="Thr" bottom-label="Thr" y-positive="top"
          />
          <StickDiagram :label="t.rightStick"
            left-label="Roll" right-label="Roll" x-positive="both"
            top-label="Pitch" bottom-label="Pitch"
          />
        </div>
      </article>

      <!-- Mode 1 (日本手 / Japanese) -->
      <article :class="['p-5 rounded-2xl border transition-all', stickMode === 1 ? 'border-darwin-amber/40 bg-darwin-panel' : 'border-white/10 bg-darwin-panel/60']">
        <div class="flex items-center gap-2 mb-3">
          <div class="w-2 h-2 rounded-full" :class="stickMode === 1 ? 'bg-darwin-amber' : 'bg-white/20'"></div>
          <h4 class="m-0 text-darwin-amber text-[0.7rem] font-bold uppercase tracking-widest">{{ t.mode1 }}</h4>
        </div>
        <p class="text-darwin-muted text-[0.65rem] mb-4">{{ t.mode1Desc }}</p>
        <div class="flex justify-center gap-6 lg:gap-10">
          <StickDiagram :label="t.leftStick"
            left-label="Pitch" right-label="Pitch" x-positive="both"
            top-label="Yaw" bottom-label="Yaw"
          />
          <StickDiagram :label="t.rightStick"
            left-label="Roll" right-label="Roll" x-positive="both"
            top-label="Thr" bottom-label="Thr" y-positive="top"
          />
        </div>
      </article>

    </div>
  </div>
</template>

<script setup>
import StickDiagram from './StickDiagram.vue'

defineProps({
  stickMode: { type: Number, required: true },
  t: { type: Object, required: true }
})
defineEmits(['update:stickMode', 'calibrate'])
</script>
