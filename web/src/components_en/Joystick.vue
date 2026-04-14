<template>
  <div class="flex flex-col items-center gap-[0.35rem] md:gap-2">
    <!-- Joystick Base -->
    <div class="relative w-[122px] h-[122px] md:w-[150px] md:h-[150px] border-2 border-[#444] rounded-full bg-black/20 shadow-[inset_0_0_0_1px_rgba(255,255,255,0.35),0_18px_36px_rgba(24,33,38,0.08)]">
      <!-- Joystick Knob -->
      <div 
        class="absolute w-[40px] h-[40px] md:w-[50px] md:h-[50px] rounded-full transform -translate-x-1/2 -translate-y-1/2 border-2 border-white/90 shadow-[0_0_10px_rgba(245,166,35,0.7),0_18px_30px_rgba(245,166,35,0.18),inset_0_0_5px_rgba(255,255,255,0.5)]"
        :style="[knobPositionStyle, knobBackgroundStyle]"
      ></div>
    </div>
    <!-- Label -->
    <div class="font-bold text-darwin-muted tracking-[0.04em] text-[0.9rem] md:text-base">
      {{ label }}
    </div>
  </div>
</template>

<script setup>
import { computed } from 'vue';

const props = defineProps({
  x: { type: Number, default: 0 }, // -100 to 100
  y: { type: Number, default: 0 }, // -100 to 100
  label: { type: String, default: 'Joystick' }
});

const knobPositionStyle = computed(() => {
  const { x, y } = props;
  
  // Constrain to circle
  const distance = Math.sqrt(x * x + y * y);
  let finalX = x;
  let finalY = y;
  
  if (distance > 100) {
    finalX = (x / distance) * 100;
    finalY = (y / distance) * 100;
  }
  
  // Calculate percentage (50% is center)
  const maxTravel = (50 / 150) * 100; 
  const left = 50 + (finalX / 100) * maxTravel;
  const top = 50 - (finalY / 100) * maxTravel;
  
  return {
    left: `${left}%`,
    top: `${top}%`
  };
});

const knobBackgroundStyle = {
  background: 'radial-gradient(circle at 35% 35%, rgba(255, 255, 255, 0.85), transparent 28%), linear-gradient(180deg, #F5A623, #5c3a0c)'
};
</script>
