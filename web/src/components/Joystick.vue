<script setup>
import { computed } from 'vue';

const props = defineProps({
  x: { type: Number, default: 0 },
  y: { type: Number, default: 0 },
  label: { type: String, default: 'Joystick' }
});

const stickStyle = computed(() => {
  const { x, y } = props;

  // Calculate distance from center [0,0] to point [x,y]
  const distance = Math.sqrt(x * x + y * y);

  let constrainedX = x;
  let constrainedY = y;

  // If distance is greater than the radius (100), clamp it to the edge
  if (distance > 100) {
    constrainedX = (x / distance) * 100;
    constrainedY = (y / distance) * 100;
  }

  // Correctly map the constrained coordinates to the available travel space.
  // The travel space for the stick's center is the base's radius minus the stick's radius.
  // Base width = 150px, Stick width = 50px.
  // Max travel from center = (150 - 50) / 2 = 50px.
  // This travel (50px) as a percentage of the base width (150px) is 33.33...%
  const travelPercentage = 50 / 150 * 100;

  const left = 50 + (constrainedX / 100) * travelPercentage;
  const top = 50 - (constrainedY / 100) * travelPercentage; // Y is inverted in CSS

  return {
    left: `${left}%`,
    top: `${top}%`,
  };
});
</script>

<template>
  <div class="joystick-container">
    <div class="joystick-base">
      <div class="joystick-stick" :style="stickStyle"></div>
    </div>
    <div class="joystick-label">{{ label }}</div>
  </div>
</template>

<style scoped>
.joystick-container {
  --joystick-base-size: 150px;
  --joystick-stick-size: 50px;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 0.5rem;
}

.joystick-base {
  position: relative;
  width: var(--joystick-base-size);
  height: var(--joystick-base-size);
  border: 2px solid var(--border-color, #444);
  border-radius: 50%;
  background: var(--joystick-surface, rgba(0, 0, 0, 0.2));
  box-shadow:
    inset 0 0 0 1px rgba(255, 255, 255, 0.35),
    0 18px 36px rgba(24, 33, 38, 0.08);
}

.joystick-stick {
  position: absolute;
  width: var(--joystick-stick-size);
  height: var(--joystick-stick-size);
  background:
    radial-gradient(circle at 35% 35%, rgba(255, 255, 255, 0.85), transparent 28%),
    linear-gradient(180deg, var(--accent-color, #00bfff), #0b4e4a);
  border-radius: 50%;
  transform: translate(-50%, -50%);
  border: 2px solid rgba(255, 255, 255, 0.92);
  box-shadow:
    0 0 10px var(--glow-color, rgba(0, 191, 255, 0.7)),
    0 18px 30px rgba(13, 127, 119, 0.18),
    inset 0 0 5px rgba(255, 255, 255, 0.5);
}

.joystick-label {
  font-weight: 700;
  color: var(--text-secondary-color, #a0a0a5);
  letter-spacing: 0.04em;
}

@media (max-width: 768px) {
  .joystick-container {
    --joystick-base-size: 122px;
    --joystick-stick-size: 40px;
    gap: 0.35rem;
  }

  .joystick-label {
    font-size: 0.9rem;
  }
}
</style>
