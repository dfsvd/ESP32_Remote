<script setup>
import { ref, computed, watch } from 'vue';

const props = defineProps({
  channelId: { type: Number, required: true },
  mappedValue: { type: Number, default: 1500 }, // 接收 mappedValue
  rawValue: { type: Number, default: 2048 },
  cal: { type: Object, required: true } 
});

const emit = defineEmits(['update:rawValue']);

const isCollapsed = ref(true);

// 1. 直接将 mappedValue 转为百分比显示，不再依赖 cal 反推
const displayPercentage = computed(() => {
  return Math.round((props.mappedValue - 1500) / 5);
});

// 你之前辛苦写的 Toggle 按钮高亮逻辑，完整保留！
const minSet = ref(false);
const midSet = ref(false);
const maxSet = ref(false);

watch(() => props.rawValue, (val) => {
  if (minSet.value) props.cal.min = val;
  if (midSet.value) props.cal.mid = val;
  if (maxSet.value) props.cal.max = val;
});

function toggleMin() { minSet.value = !minSet.value; if (minSet.value) props.cal.min = props.rawValue; }
function toggleMid() { midSet.value = !midSet.value; if (midSet.value) props.cal.mid = props.rawValue; }
function toggleMax() { maxSet.value = !maxSet.value; if (maxSet.value) props.cal.max = props.rawValue; }

</script>

<template>
  <div class="aux-channel">
    <div class="main-display">
      <label class="channel-label">CH{{ channelId }}</label>
      
      <div class="slider-container">
        <input 
          type="range" 
          min="-100" 
          max="100" 
          :value="displayPercentage" 
          disabled
          class="percentage-slider"
        >
      </div>
      
      <span class="channel-value">{{ displayPercentage }}%</span>
      <button class="toggle-btn" @click="isCollapsed = !isCollapsed">
        {{ isCollapsed ? '▶' : '▼' }}
      </button>
    </div>
    
    <div v-if="!isCollapsed" class="details">
      <div class="raw-value">原始值: {{ rawValue }}</div>
      
      <div class="control-group raw-slider-group">
        <input 
          type="range" 
          min="0" 
          max="4095" 
          :value="rawValue" 
          @input="$emit('update:rawValue', parseInt($event.target.value, 10))"
          class="raw-slider"
        >
      </div>

      <div class="control-group">
        <label>Min:</label>
        <input type="number" v-model.number="cal.min" :disabled="!minSet" />
        <div class="toggle-switch" :class="{ on: minSet }" @click="toggleMin">
          <div class="toggle-thumb"></div>
        </div>
      </div>
      <div class="control-group">
        <label>Mid:</label>
        <input type="number" v-model.number="cal.mid" :disabled="!midSet" />
        <div class="toggle-switch" :class="{ on: midSet }" @click="toggleMid">
          <div class="toggle-thumb"></div>
        </div>
      </div>
      <div class="control-group">
        <label>Max:</label>
        <input type="number" v-model.number="cal.max" :disabled="!maxSet" />
        <div class="toggle-switch" :class="{ on: maxSet }" @click="toggleMax">
          <div class="toggle-thumb"></div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.aux-channel {
  background: var(--channel-card-bg, #2c2c2e);
  border: 1px solid var(--channel-card-border, rgba(255, 255, 255, 0.08));
  border-radius: 18px;
  padding: 0.7rem 0.95rem;
  width: 100%;
  max-width: none;
  box-sizing: border-box;
  box-shadow: 0 14px 28px rgba(24, 33, 38, 0.06);
}
.main-display {
  display: flex;
  align-items: center;
  gap: 0.85rem;
}
.channel-label {
  font-weight: bold;
  width: 50px;
  color: var(--text-secondary-color, #a0a0a5);
}
.slider-container { flex-grow: 1; display: flex; align-items: center; }

/* ======== 上方百分比滑块的样式 ======== */
.percentage-slider {
  -webkit-appearance: none;
  appearance: none;
  width: 100%;
  height: 10px;
  background: linear-gradient(90deg, rgba(13, 127, 119, 0.1), rgba(197, 122, 52, 0.16));
  border-radius: 5px;
  border: 1px solid var(--border-color, #444);
  outline: none;
  margin: 0;
}
.percentage-slider::-webkit-slider-thumb {
  -webkit-appearance: none;
  appearance: none;
  width: 20px;
  height: 20px;
  background: var(--accent-color, #00bfff);
  border-radius: 50%;
  cursor: pointer;
  box-shadow: 0 0 8px var(--glow-color, rgba(0, 191, 255, 0.7));
}
.percentage-slider::-moz-range-thumb {
  width: 20px;
  height: 20px;
  background: var(--accent-color, #00bfff);
  border-radius: 50%;
  cursor: pointer;
  box-shadow: 0 0 8px var(--glow-color, rgba(0, 191, 255, 0.7));
  border: none;
}
/* ====================================== */

.channel-value {
  font-family: monospace;
  width: 50px;
  text-align: right;
  color: var(--text-color, #f0f0f5);
}
.toggle-btn {
  background: none;
  border: none;
  cursor: pointer;
  font-size: 1.2rem;
  padding: 0 0.5rem;
  color: var(--text-secondary-color, #a0a0a5);
}
.details {
  margin-top: 1rem;
  padding-top: 1rem;
  border-top: 1px solid var(--border-color, #444);
  display: flex;
  flex-direction: column;
  gap: 0.8rem;
}
.control-group {
  display: grid;
  grid-template-columns: 40px minmax(0, 1fr) 72px;
  gap: 0.7rem;
  align-items: center;
}
.raw-slider-group {
  grid-template-columns: 1fr;
  padding: 0.2rem 0;
  width: 100%;
  min-width: 0;
}
input[type="number"] {
  width: 100%;
  padding: 0.25rem;
  background-color: var(--input-bg, #333);
  border: 1px solid var(--border-color, #555);
  color: var(--text-color, #f0f0f5);
  border-radius: 10px;
}
button {
  padding: 0.25rem 0.5rem;
  background-color: var(--accent-color, #00bfff);
  color: #f5fffe;
  border: none;
  border-radius: 4px;
  cursor: pointer;
}
button.set-active {
  background-color: #00e676;
  color: #111;
}
.edit-toggle-row {
  display: flex;
  justify-content: flex-end;
  margin-bottom: 0.25rem;
}
button.edit-toggle {
  background-color: #555;
  color: #ccc;
}
button.edit-toggle.edit-on {
  background-color: #f0a500;
  color: #111;
}
button:disabled {
  opacity: 0.35;
  cursor: not-allowed;
}

/* Toggle switch 保留原有样式 */
.toggle-switch {
  width: 36px;
  height: 20px;
  background: rgba(108, 118, 115, 0.5);
  border-radius: 10px;
  position: relative;
  cursor: pointer;
  transition: background 0.25s;
  flex-shrink: 0;
}
.toggle-switch.on {
  background: #00e676;
}
.toggle-thumb {
  width: 16px;
  height: 16px;
  background: #fff;
  border-radius: 50%;
  position: absolute;
  top: 2px;
  left: 2px;
  transition: left 0.25s;
}
.toggle-switch.on .toggle-thumb {
  left: 18px;
}
.raw-value { 
  text-align: center; 
  color: var(--text-secondary-color, #aaa); 
  font-size: 0.9rem;
  font-family: monospace;
}

/* 底部原始值滑块的样式 */
.raw-slider {
  -webkit-appearance: none;
  appearance: none;
  display: block;
  width: 100%;
  min-width: 0;
  height: 8px;
  background: linear-gradient(90deg, rgba(13, 127, 119, 0.16), rgba(197, 122, 52, 0.16));
  outline: none;
  border-radius: 4px;
  transition: opacity .2s;
}
.raw-slider::-webkit-slider-thumb {
  -webkit-appearance: none;
  appearance: none;
  width: 18px;
  height: 18px;
  background: #f0f0f5;
  cursor: pointer;
  border-radius: 50%;
  border: 2px solid var(--accent-color, #00bfff);
}
.raw-slider::-moz-range-thumb {
  width: 18px;
  height: 18px;
  background: #f0f0f5;
  cursor: pointer;
  border-radius: 50%;
  border: 2px solid var(--accent-color, #00bfff);
}

@media (max-width: 768px) {
  .aux-channel {
    max-width: none;
    padding: 0.65rem 0.75rem;
  }

  .main-display {
    gap: 0.65rem;
  }

  .channel-label {
    width: 42px;
    font-size: 0.92rem;
  }

  .channel-value {
    width: 44px;
    font-size: 0.88rem;
  }

  .toggle-btn {
    padding: 0 0.2rem;
  }

  .details {
    margin-top: 0.8rem;
    padding-top: 0.8rem;
    gap: 0.7rem;
  }

  .control-group {
    grid-template-columns: 42px minmax(0, 1fr) 72px;
    gap: 0.65rem;
  }

  .raw-slider-group {
    grid-template-columns: minmax(0, 1fr);
  }

  input[type="number"] {
    min-width: 0;
    font-size: 0.95rem;
  }

  .raw-value {
    font-size: 0.82rem;
  }
}
</style>
