<template>
  <div class="grid gap-4">
    <!-- Save New Profile -->
    <div class="flex items-center gap-3 p-4 rounded-2xl border border-[var(--theme-border)] bg-darwin-panel">
      <input
        v-model="newName"
        type="text"
        :placeholder="t.namePlaceholder"
        class="flex-1 px-3 py-2 text-sm rounded-lg bg-darwin-panel border border-[var(--theme-border)] text-darwin-ink outline-none"
        @keyup.enter="doSave"
      />
      <button
        type="button"
        @click="doSave"
        class="shrink-0 px-4 py-2 text-sm font-bold rounded-full bg-gradient-to-br from-darwin-amber to-darwin-orange text-black"
      >{{ t.save }}</button>
    </div>

    <!-- Profile List -->
    <div v-if="profiles.length === 0" class="p-4 text-center text-darwin-muted text-sm rounded-2xl border border-dashed border-[var(--theme-border)]">
      {{ t.empty }}
    </div>

    <div v-for="(prof, idx) in profiles" :key="idx"
      class="flex items-center gap-3 p-3 rounded-2xl border border-[var(--theme-border)] bg-darwin-panel"
    >
      <span class="flex-1 text-sm font-bold text-darwin-ink truncate">{{ prof.name }}</span>

      <!-- Confirm state -->
      <template v-if="confirmIdx === idx">
        <span class="text-xs text-darwin-muted">{{ t.confirmPrompt }}</span>
        <button type="button" @click="doLoad(prof.name)"
          class="px-3 py-1 text-xs font-bold rounded-full bg-darwin-amber/20 text-darwin-amber border border-darwin-amber/30"
        >{{ t.confirmLoad }}</button>
        <button type="button" @click="confirmIdx = -1"
          class="px-3 py-1 text-xs font-bold rounded-full border border-[var(--theme-border)] text-darwin-muted"
        >{{ t.cancel }}</button>
      </template>

      <!-- Delete confirm -->
      <template v-else-if="deleteIdx === idx">
        <span class="text-xs text-darwin-muted">{{ t.confirmDel }}</span>
        <button type="button" @click="doDelete(prof.name)"
          class="px-3 py-1 text-xs font-bold rounded-full bg-red-500/20 text-red-400 border border-red-500/30"
        >{{ t.confirmDelete }}</button>
        <button type="button" @click="deleteIdx = -1"
          class="px-3 py-1 text-xs font-bold rounded-full border border-[var(--theme-border)] text-darwin-muted"
        >{{ t.cancel }}</button>
      </template>

      <!-- Default actions -->
      <template v-else>
        <button type="button" @click="confirmIdx = idx"
          class="px-3 py-1 text-xs font-bold rounded-full bg-darwin-amber/20 text-darwin-amber border border-darwin-amber/30"
        >{{ t.load }}</button>
        <button type="button" @click="deleteIdx = idx"
          class="px-3 py-1 text-xs font-bold rounded-full border border-[var(--theme-border)] text-darwin-muted"
        >{{ t.delete }}</button>
      </template>
    </div>
  </div>
</template>

<script setup>
import { ref } from 'vue'

const props = defineProps({
  profiles: { type: Array, required: true },
  t: { type: Object, required: true }
})

const emit = defineEmits(['save', 'load', 'delete'])

const newName = ref('')
const confirmIdx = ref(-1)
const deleteIdx = ref(-1)

function doSave() {
  const name = newName.value.trim()
  if (!name) return
  emit('save', name)
  newName.value = ''
}

function doLoad(name) {
  confirmIdx.value = -1
  emit('load', name)
}

function doDelete(name) {
  deleteIdx.value = -1
  emit('delete', name)
}
</script>
