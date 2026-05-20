<template>
  <div class="grid gap-4">
    <!-- Actions -->
    <div class="flex items-center gap-3">
      <button
        type="button"
        @click="$emit('write')"
        class="px-5 py-2 text-sm font-bold rounded-full bg-darwin-amber/20 text-darwin-amber border border-darwin-amber/30"
      >
        {{ t.write }}
      </button>
      <button
        type="button"
        @click="$emit('reset')"
        class="px-5 py-2 text-sm font-bold rounded-full border border-[var(--theme-border)] text-darwin-muted"
      >
        {{ t.reset }}
      </button>
    </div>

    <!-- Mapping Table -->
    <div class="overflow-x-auto rounded-2xl border border-[var(--theme-border)]">
      <table class="w-full text-sm">
        <thead>
          <tr class="bg-[var(--theme-bg-subtle)]">
            <th class="text-left p-3 text-darwin-muted font-bold uppercase tracking-widest text-[0.7rem]">{{ t.columnChannel }}</th>
            <th class="text-left p-3 text-darwin-muted font-bold uppercase tracking-widest text-[0.7rem]">{{ t.columnDefault }}</th>
            <th class="text-left p-3 text-darwin-muted font-bold uppercase tracking-widest text-[0.7rem]">{{ t.columnCurrent }}</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="(m, idx) in mappings" :key="m.channel" class="border-t border-[var(--theme-border-light)]">
            <td class="p-3 text-[var(--theme-text)] font-bold">CH{{ m.channel }}</td>
            <td class="p-3 text-darwin-muted">{{ m.default }}</td>
            <td class="p-3">
              <select
                :value="m.current"
                @change="$emit('update:mapping', idx, $event.target.value)"
                class="w-full max-w-[120px] px-2.5 py-1.5 text-sm rounded-full bg-darwin-panel border border-[var(--theme-border)] text-darwin-ink outline-none"
              >
                <option v-for="opt in switchOptions" :key="opt" :value="opt">{{ opt }}</option>
              </select>
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<script setup>
defineProps({
  mappings: { type: Array, required: true },
  switchOptions: { type: Array, required: true },
  t: { type: Object, required: true }
})

defineEmits(['write', 'reset', 'update:mapping'])
</script>
