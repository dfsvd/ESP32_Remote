<template>
  <div class="grid gap-4">
    <!-- Actions -->
    <div class="flex items-center gap-3">
      <button
        type="button"
        @click="openDialog('write')"
        class="px-5 py-2 text-sm font-bold rounded-full bg-darwin-amber/20 text-darwin-amber border border-darwin-amber/30"
      >
        {{ t.write }}
      </button>
      <button
        type="button"
        @click="openDialog('reset')"
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

    <!-- Confirm Dialog Overlay -->
    <Teleport to="body">
      <div v-if="dialog" class="fixed inset-0 z-[100] flex items-center justify-center bg-black/50 backdrop-blur-sm" @click.self="closeDialog">
        <div class="bg-darwin-panel border border-[var(--theme-border)] rounded-2xl p-6 w-[320px] shadow-2xl flex flex-col gap-4">

          <!-- Phase: confirm -->
          <template v-if="dialog.phase === 'confirm'">
            <h3 class="text-lg font-bold text-darwin-ink text-center m-0">
              {{ dialog.action === 'write' ? t.confirmWrite : t.confirmReset }}
            </h3>
            <p class="text-sm text-darwin-muted text-center m-0">
              {{ dialog.action === 'write' ? t.confirmWriteDesc : t.confirmResetDesc }}
            </p>
            <div class="flex gap-3 justify-center">
              <button
                type="button"
                @click="doAction"
                class="px-6 py-2 text-sm font-bold rounded-full bg-darwin-amber text-black"
              >
                {{ t.confirmOk }}
              </button>
              <button
                type="button"
                @click="closeDialog"
                class="px-6 py-2 text-sm font-bold rounded-full border border-[var(--theme-border)] text-darwin-muted"
              >
                {{ t.confirmCancel }}
              </button>
            </div>
          </template>

          <!-- Phase: sending -->
          <template v-else-if="dialog.phase === 'sending'">
            <div class="flex flex-col items-center gap-3 py-4">
              <span class="inline-block w-8 h-8 border-3 border-darwin-amber/30 border-t-darwin-amber rounded-full animate-spin"></span>
              <span class="text-sm text-darwin-amber font-bold">{{ dialog.action === 'write' ? t.sendingWrite : t.sendingReset }}</span>
            </div>
          </template>

          <!-- Phase: done -->
          <template v-else-if="dialog.phase === 'done'">
            <div class="flex flex-col items-center gap-3 py-4">
              <span class="text-3xl">✓</span>
              <span class="text-sm text-green-400 font-bold">{{ dialog.action === 'write' ? t.doneWrite : t.doneReset }}</span>
            </div>
          </template>

        </div>
      </div>
    </Teleport>
  </div>
</template>

<script setup>
import { ref, watch } from 'vue'

const props = defineProps({
  mappings: { type: Array, required: true },
  switchOptions: { type: Array, required: true },
  writeState: { type: String, default: 'idle' },
  t: { type: Object, required: true }
})

const emit = defineEmits(['write', 'reset', 'update:mapping'])

const dialog = ref(null) // null | { action: 'write' | 'reset', phase: 'confirm' | 'sending' | 'done' }

let sendTimeout = null

function openDialog(action) {
  dialog.value = { action, phase: 'confirm' }
}

function closeDialog() {
  if (dialog.value?.phase === 'sending') return
  clearTimeout(sendTimeout)
  dialog.value = null
}

function doAction() {
  if (!dialog.value) return
  dialog.value.phase = 'sending'
  emit(dialog.value.action)

  sendTimeout = setTimeout(() => {
    if (dialog.value?.phase === 'sending') dialog.value = null
  }, 5000)
}

watch(() => props.writeState, (val) => {
  if (val === 'ok' && dialog.value?.phase === 'sending') {
    clearTimeout(sendTimeout)
    dialog.value.phase = 'done'
    setTimeout(() => { dialog.value = null }, 1200)
  }
})
</script>
