<template>
  <div>
    <!-- Title -->
    <div class="flex items-center gap-2 mb-4">
      <span class="w-1 h-4 rounded-full bg-darwin-amber shrink-0"></span>
      <span class="text-sm font-bold text-darwin-ink">{{ t.title }}</span>
    </div>

    <!-- Save / Import Actions -->
    <div class="flex items-center gap-3 pb-4 mb-4 border-b border-dashed border-[var(--theme-border)]">
      <input
        v-model="newName"
        type="text"
        :placeholder="t.namePlaceholder"
        class="flex-1 h-10 px-3 text-sm rounded-lg border border-[var(--theme-border)] bg-darwin-panel text-darwin-ink outline-none transition-all focus:border-darwin-amber focus:shadow-[0_0_0_3px_rgba(245,158,11,0.15)]"
        @keyup.enter="doSave"
      />
      <!-- Overwrite confirm -->
      <template v-if="overwriteName">
        <span class="text-xs text-darwin-muted shrink-0">{{ overwriteName }}: {{ t.confirmOverwrite }}</span>
        <button type="button" @click="confirmOverwrite"
          class="h-8 px-4 text-xs font-bold rounded-md transition-all duration-150 bg-darwin-amber/20 text-darwin-amber border border-darwin-amber/30 hover:bg-darwin-amber/30"
        >{{ t.confirmOverwriteBtn }}</button>
        <button type="button" @click="overwriteName = ''"
          class="h-8 px-4 text-xs font-bold rounded-md transition-all duration-150 border border-[var(--theme-border)] text-darwin-muted hover:text-darwin-ink"
        >{{ t.cancel }}</button>
      </template>
      <template v-else>
        <button type="button" @click="doSave"
          class="h-10 px-5 text-sm font-bold rounded-lg bg-gradient-to-br from-darwin-amber to-darwin-orange text-black hover:brightness-110 transition-all"
        >{{ t.save }}</button>
      </template>
      <button type="button" @click="$refs.fileInput.click()"
        class="h-10 px-5 text-sm font-medium rounded-lg border border-[var(--theme-border)] bg-darwin-panel text-darwin-ink hover:bg-[var(--theme-bg-hover)] transition-all"
      >{{ t.import_ }}</button>
      <input ref="fileInput" type="file" accept=".json" class="hidden" @change="onFilePicked" />
    </div>

    <!-- Import success/error toast -->
    <div v-if="importSuccess" class="p-3 rounded-lg bg-green-500/20 border border-green-500/30 text-green-400 text-sm text-center font-bold">
      {{ t.importSuccess }}
    </div>
    <div v-if="importError" class="p-3 rounded-lg bg-red-500/20 border border-red-500/30 text-red-400 text-sm text-center font-bold">
      {{ importError }}
    </div>

    <!-- Profile List -->
    <div v-if="profiles.length === 0" class="p-4 text-center text-darwin-muted text-sm rounded-lg border border-dashed border-[var(--theme-border)]">
      {{ t.empty }}
    </div>

    <div v-else class="flex flex-col gap-2">
      <div v-for="(prof, idx) in profiles" :key="idx"
        class="flex items-center justify-between p-3 rounded-lg border border-[var(--theme-border)] bg-darwin-panel transition-all hover:border-[var(--theme-border-hover)] hover:shadow-sm"
      >
      <!-- Rename mode -->
      <template v-if="renameIdx === idx">
        <input
          ref="renameInput"
          v-model="renameVal"
          type="text"
          class="flex-1 h-8 px-2 text-sm rounded bg-darwin-panel border border-darwin-amber/50 text-darwin-ink outline-none"
          @keyup.enter="doRename(prof.name)"
          @keyup.escape="renameIdx = -1"
        />
        <div class="flex items-center gap-2 ml-3 shrink-0">
          <button type="button" @click="doRename(prof.name)"
            class="h-7 px-2.5 text-xs font-medium rounded-md transition-all duration-150 bg-darwin-amber/20 text-darwin-amber border border-darwin-amber/30 hover:bg-darwin-amber/30"
          >{{ t.confirmRename }}</button>
          <button type="button" @click="renameIdx = -1"
            class="h-7 px-2.5 text-xs font-medium rounded-md transition-all duration-150 border border-[var(--theme-border)] text-darwin-muted hover:text-darwin-ink"
          >{{ t.cancel }}</button>
        </div>
      </template>

      <!-- Load confirm -->
      <template v-else-if="confirmIdx === idx">
        <span class="text-xs text-darwin-muted truncate">{{ prof.name }}: {{ t.confirmPrompt }}</span>
        <div class="flex items-center gap-2 ml-3 shrink-0">
          <button type="button" @click="doLoad(prof.name)"
            class="h-7 px-2.5 text-xs font-medium rounded-md transition-all duration-150 bg-darwin-amber/20 text-darwin-amber border border-darwin-amber/30 hover:bg-darwin-amber/30"
          >{{ t.confirmLoad }}</button>
          <button type="button" @click="confirmIdx = -1"
            class="h-7 px-2.5 text-xs font-medium rounded-md transition-all duration-150 border border-[var(--theme-border)] text-darwin-muted hover:text-darwin-ink"
          >{{ t.cancel }}</button>
        </div>
      </template>

      <!-- Delete confirm -->
      <template v-else-if="deleteIdx === idx">
        <span class="text-xs text-darwin-muted truncate">{{ prof.name }}: {{ t.confirmDel }}</span>
        <div class="flex items-center gap-2 ml-3 shrink-0">
          <button type="button" @click="doDelete(prof.name)"
            class="h-7 px-2.5 text-xs font-medium rounded-md transition-all duration-150 bg-red-500/20 text-red-400 border border-red-500/30 hover:bg-red-500/30"
          >{{ t.confirmDelete }}</button>
          <button type="button" @click="deleteIdx = -1"
            class="h-7 px-2.5 text-xs font-medium rounded-md transition-all duration-150 border border-[var(--theme-border)] text-darwin-muted hover:text-darwin-ink"
          >{{ t.cancel }}</button>
        </div>
      </template>

      <!-- Default actions -->
      <template v-else>
        <span class="text-sm font-medium text-darwin-ink truncate">{{ prof.name }}</span>
        <div class="flex items-center gap-2 shrink-0">
          <button type="button" @click="$emit('exportProfile', prof.name)"
            class="h-7 px-2.5 text-xs font-medium rounded-md transition-all duration-150 border border-[var(--theme-border)] text-darwin-muted hover:text-darwin-ink"
          >{{ t.export }}</button>
          <button type="button" @click="startRename(idx, prof.name)"
            class="h-7 px-2.5 text-xs font-medium rounded-md transition-all duration-150 border border-[var(--theme-border)] text-darwin-muted hover:text-darwin-ink"
          >{{ t.rename }}</button>
          <button type="button" @click="confirmIdx = idx"
            class="h-7 px-2.5 text-xs font-medium rounded-md transition-all duration-150 border border-darwin-amber/30 text-darwin-amber hover:bg-darwin-amber/10"
          >{{ t.load }}</button>
          <button type="button" @click="deleteIdx = idx"
            class="h-7 px-2.5 text-xs font-medium rounded-md transition-all duration-150 border border-[var(--theme-border)] text-darwin-muted hover:text-red-400 hover:border-red-500/30 hover:bg-red-500/10"
          >{{ t.delete }}</button>
        </div>
      </template>
    </div>
  </div>
</div>
</template>

<script setup>
import { ref, nextTick } from 'vue'

const props = defineProps({
  profiles: { type: Array, required: true },
  t: { type: Object, required: true },
  importSuccess: { type: Boolean, default: false },
  importError: { type: String, default: '' }
})

const emit = defineEmits(['save', 'load', 'delete', 'rename', 'import', 'exportProfile'])

const newName = ref('')
const overwriteName = ref('')
const confirmIdx = ref(-1)
const deleteIdx = ref(-1)
const renameIdx = ref(-1)
const renameVal = ref('')
const renameInput = ref(null)
const fileInput = ref(null)

function doSave() {
  const name = newName.value.trim()
  if (!name) return
  const exists = props.profiles.some(p => p.name === name)
  if (exists) {
    overwriteName.value = name
    return
  }
  emit('save', name)
  newName.value = ''
}

function confirmOverwrite() {
  emit('save', overwriteName.value)
  overwriteName.value = ''
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

function startRename(idx, name) {
  renameIdx.value = idx
  renameVal.value = name
  nextTick(() => renameInput.value?.focus())
}

function doRename(oldName) {
  const name = renameVal.value.trim()
  if (!name || name === oldName) { renameIdx.value = -1; return }
  emit('rename', oldName, name)
  renameIdx.value = -1
}

function onFilePicked(e) {
  const file = e.target.files?.[0]
  if (!file) return
  const reader = new FileReader()
  reader.onload = () => {
    emit('import', reader.result)
    fileInput.value.value = ''
  }
  reader.readAsText(file)
}
</script>
