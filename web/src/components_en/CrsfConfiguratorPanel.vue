<template>
  <section class="grid gap-[18px] p-6 rounded-[28px] text-darwin-ink bg-[radial-gradient(circle_at_top_left,rgba(245,166,35,0.1),transparent_35%),radial-gradient(circle_at_top_right,rgba(217,119,6,0.1),transparent_28%),linear-gradient(180deg,rgba(45,45,45,0.9),rgba(35,35,35,0.9))] shadow-[0_28px_80px_rgba(0,0,0,0.4)] border border-white/5">
    <!-- Header: Controls & Global Status -->
    <header class="flex flex-col lg:flex-row items-stretch justify-between gap-4 p-4 lg:p-[18px] rounded-[24px] bg-gradient-to-br from-white/10 to-transparent border border-white/10">
      <div class="flex items-center gap-3 flex-wrap">
        <button 
          class="px-[18px] py-[12px] bg-white/10 border border-white/10 text-darwin-ink font-bold rounded-full cursor-pointer transition-all hover:-translate-y-px hover:bg-white/20"
          type="button"
          @click="$emit('refresh')"
        >
          {{ t.refresh }}
        </button>
        <button 
          class="px-[18px] py-[12px] border border-transparent bg-gradient-to-br from-darwin-amber to-darwin-orange text-black font-bold rounded-full shadow-[0_16px_30px_rgba(245,166,35,0.2)] cursor-pointer transition-all hover:-translate-y-px disabled:opacity-60 disabled:cursor-not-allowed"
          type="button"
          :disabled="loading || !bindItem"
          @click="handleBind"
        >
          {{ loading ? t.processing : t.startBind }}
        </button>
      </div>

      <section class="grid grid-cols-3 gap-2.5 lg:gap-[14px] flex-auto">
        <article class="p-3.5 lg:p-[18px] rounded-[18px] text-center lg:text-left bg-[#1f1f22] border border-white/5">
          <span class="block mb-1.5 lg:mb-2 text-[11px] lg:text-xs uppercase tracking-[0.16em] text-darwin-muted">{{ t.link }}</span>
          <strong :class="['block text-[18px] lg:text-[26px] leading-[1.15]', status.isLinked ? 'text-darwin-amber' : 'text-[#8b6f5d]']">
            {{ status.isLinked ? t.online : t.offline }}
          </strong>
        </article>
        <article class="p-3.5 lg:p-[18px] rounded-[18px] text-center lg:text-left bg-[#1f1f22] border border-white/5">
          <span class="block mb-1.5 lg:mb-2 text-[11px] lg:text-xs uppercase tracking-[0.16em] text-darwin-muted">{{ t.signal }}</span>
          <strong class="block text-[18px] lg:text-[26px] leading-[1.15] text-darwin-ink">{{ status.rssi ?? '--' }}</strong>
        </article>
        <article class="p-3.5 lg:p-[18px] rounded-[18px] text-center lg:text-left bg-[#1f1f22] border border-white/5">
          <span class="block mb-1.5 lg:mb-2 text-[11px] lg:text-xs uppercase tracking-[0.16em] text-darwin-muted">{{ t.menuSync }}</span>
          <strong class="block text-[18px] lg:text-[26px] leading-[1.15] text-darwin-ink">{{ status.loadedParams ?? 0 }} / {{ status.totalParams ?? 0 }}</strong>
        </article>
      </section>
    </header>

    <!-- Menu Tree -->
    <section class="p-3.5 lg:p-[18px] rounded-[26px] bg-white/5 border border-white/10">
      <aside class="p-3.5 lg:p-[18px] rounded-[22px] bg-[#1a1a1c] border border-white/5">
        <ul class="grid gap-2.5 p-0 m-0 list-none">
          <li 
            v-for="node in flattenedMenuTree" 
            :key="node.item.id" 
            class="grid gap-0"
            :style="{ '--menu-depth': node.depth }"
          >
            <!-- Menu Item Button -->
            <button 
              type="button"
              :class="[
                'w-full grid grid-cols-[minmax(0,1fr)_auto] items-center gap-[10px] lg:gap-[14px] text-left p-[13px_12px] lg:p-[14px] rounded-[18px] border border-transparent bg-white/5 text-darwin-ink cursor-pointer transition-all hover:-translate-y-px hover:border-darwin-amber/30 hover:shadow-[0_14px_26px_rgba(0,0,0,0.5)]',
                {
                  '!-translate-y-px !border-darwin-amber/30 !shadow-[0_14px_26px_rgba(0,0,0,0.5)] bg-gradient-to-b from-darwin-amber/10 to-transparent': isSelected(node.item.id),
                  'bg-gradient-to-b from-white/10 to-white/5': node.item.kind === 'folder' && isSelected(node.item.id),
                  '!bg-white/10': node.depth > 0
                }
              ]"
              :style="{ paddingLeft: `calc(12px + (${node.depth} * 18px))` }"
              @click="toggleItem(node.item)"
            >
              <span class="flex items-center gap-3 min-w-0">
                <span class="flex-shrink-0 grid place-items-center w-[34px] h-[34px] lg:w-[38px] lg:h-[38px] rounded-[10px] lg:rounded-[12px] bg-darwin-amber/10 text-darwin-amber text-xs font-bold">
                  {{ getKindIcon(node.item.kind) }}
                </span>
                <span class="grid align-content-center min-w-0">
                  <strong class="block leading-[1.2] break-words">{{ node.item.name }}</strong>
                  <small class="block mt-1 text-xs text-darwin-muted">#{{ node.item.id }} · {{ node.item.kindLabel }}</small>
                </span>
              </span>
              
              <span class="flex items-center self-center justify-self-end gap-2 flex-shrink-0">
                <span v-if="node.item.kind === 'select'" class="inline-flex items-center justify-center px-2.5 py-1.5 rounded-full text-[11px] lg:text-xs font-bold whitespace-nowrap bg-darwin-amber/10 text-darwin-amber max-w-full">
                  {{ getSelectLabel(node.item) }}
                </span>
                <span v-else-if="node.item.kind === 'info'" class="inline-flex items-center justify-center px-2.5 py-1.5 rounded-full text-[11px] lg:text-xs font-bold whitespace-nowrap bg-white/10 text-darwin-muted max-w-full">
                  {{ t.readOnly }}
                </span>
                <span v-else-if="node.item.kind === 'command'" class="inline-flex items-center justify-center px-2.5 py-1.5 rounded-full text-[11px] lg:text-xs font-bold whitespace-nowrap bg-[#cb7a2f]/20 text-[#d97706] max-w-full">
                  {{ t.command }}
                </span>
                <span 
                  :class="['text-[18px] text-darwin-amber transition-transform duration-160', isSelected(node.item.id) ? 'rotate-90' : '']"
                  :style="{ transform: isSelected(node.item.id) ? 'rotate(0deg)' : 'rotate(-90deg)' }"
                >
                  ▾
                </span>
              </span>
            </button>

            <!-- Expanded Content -->
            <section 
              v-if="isSelected(node.item.id) && node.item.kind !== 'folder'"
              class="grid gap-2.5"
              :style="{ paddingLeft: `calc(12px + (${node.depth} * 18px))` }"
            >
              <!-- Select Editor -->
              <section v-if="node.item.kind === 'select'" class="p-4 mt-2.5 border border-white/5 rounded-[18px] bg-[#1f1f22]">
                <div class="grid grid-cols-2 sm:grid-cols-[repeat(auto-fit,minmax(140px,1fr))] gap-3">
                  <button 
                    v-for="opt in node.item.optionItems" 
                    :key="`${node.item.id}-${opt.index}`"
                    type="button"
                    :class="[
                      'min-h-[90px] grid content-between p-3.5 rounded-[18px] border border-white/10 bg-white/5 text-left cursor-pointer text-darwin-ink transition-all hover:-translate-y-px hover:border-darwin-amber/40 hover:shadow-[0_14px_26px_rgba(0,0,0,0.4)]',
                      { 'border-darwin-amber/50 bg-gradient-to-b from-darwin-amber/10 to-transparent shadow-[0_14px_26px_rgba(0,0,0,0.4)]': opt.index === node.item.value }
                    ]"
                    @click.stop="handleSelect(node.item, opt)"
                  >
                    <span class="inline-flex w-fit px-2 py-1 rounded-full bg-black/20 text-[11px] tracking-[0.08em] uppercase text-darwin-muted">{{ opt.index }}</span>
                    <strong class="block leading-[1.2] break-words">{{ opt.label }}</strong>
                  </button>
                </div>
              </section>

              <!-- Command Editor -->
              <section v-else-if="node.item.kind === 'command'" class="p-4 mt-2.5 border border-white/5 rounded-[18px] bg-[#1f1f22]">
                <div class="flex items-center gap-3 flex-wrap">
                  <button 
                    class="max-w-full px-[18px] py-[12px] border border-transparent bg-gradient-to-br from-darwin-amber to-darwin-orange text-black font-bold rounded-[22px] whitespace-normal break-words shadow-[0_16px_30px_rgba(245,166,35,0.2)] cursor-pointer transition-all hover:-translate-y-px disabled:opacity-60 disabled:cursor-not-allowed"
                    type="button"
                    :disabled="loading"
                    @click.stop="handleCommand(node.item)"
                  >
                    {{ getCommandLabel(node.item) }}
                  </button>
                </div>
              </section>

              <!-- Info/Text Editor -->
              <section v-else-if="node.item.kind === 'info'" class="p-4 mt-2.5 border border-white/5 rounded-[18px] bg-[#1f1f22]">
                <div class="p-4 rounded-[16px] bg-black/30 border border-white/5 text-darwin-muted font-mono break-words">
                  {{ node.item.content || '-' }}
                </div>
              </section>
              <section v-else class="p-4 mt-2.5 border border-white/5 rounded-[18px] bg-[#1f1f22]">
                <div class="p-4 rounded-[16px] bg-black/30 border border-white/5 text-darwin-muted font-mono break-words">
                  {{ node.item.content || 'No preview available.' }}
                </div>
              </section>
            </section>
          </li>
        </ul>
      </aside>
    </section>
  </section>
</template>

<script setup>
import { ref, computed, watch } from 'vue';

const props = defineProps({
  loading: { type: Boolean, default: false },
  status: { type: Object, default: () => ({ isReady: false, isLinked: false, rssi: null, lq: null, loadedParams: 0, totalParams: 0, deviceLabel: '' }) },
  menus: { type: Array, default: () => [] },
  t: { type: Object, default: () => ({}) }
});

const emit = defineEmits(['refresh', 'bind', 'command', 'select-change', 'item-selected', 'folder-open']);

function getKind(type) {
  if (typeof type === 'string') return type;
  switch (type) {
    case 9: return 'select';
    case 11: return 'folder';
    case 12: return 'info';
    case 13: return 'command';
    case 10: return 'text';
    default: return 'unknown';
  }
}

function getLabelForKind(kind) {
  return {
    select: props.t.select,
    folder: props.t.folder,
    command: props.t.command,
    info: props.t.info,
    text: props.t.text
  }[kind] || props.t.general;
}

function parseOptions(options) {
  if (Array.isArray(options)) {
    return options.map((label, index) => ({
      index,
      rawLabel: String(label ?? "").trim(),
      label: compactOptionLabel(label)
    }));
  }
  if (!options) return [];
  return String(options)
    .split(';')
    .map((label, index) => ({
      index,
      rawLabel: label.trim(),
      label: compactOptionLabel(label)
    }))
    .filter(o => o.label.length > 0);
}

function compactOptionLabel(label) {
  const text = String(label ?? '').trim();
  if (!text) return '';
  const withoutDbm = text.replace(/\s*\([^)]*\)\s*$/u, '').trim();
  return withoutDbm || text;
}

function isBindAction(name = '') {
  const n = String(name).toLowerCase();
  return n.includes('bind') || n.includes('对频');
}

const normalizedMenus = computed(() => {
  return props.menus.map(m => {
    const kind = getKind(m.type ?? m.kind);
    return {
      id: m.id,
      parentId: m.parentId ?? m.parent_id ?? 0,
      name: m.name || `Item ${m.id}`,
      kind: kind,
      kindLabel: getLabelForKind(kind),
      value: Number.isFinite(m.value) ? m.value : 0,
      content: m.content ?? m.options ?? '',
      optionItems: parseOptions(m.options),
      raw: m
    };
  }).sort((a, b) => a.id - b.id);
});

const menuMap = computed(() => {
  const map = new Map();
  for (const item of normalizedMenus.value) {
    if (!map.has(item.parentId)) map.set(item.parentId, []);
    map.get(item.parentId).push(item);
  }
  return map;
});

const expandedIds = ref(new Set());

const flattenedMenuTree = computed(() => {
  const list = [];
  const build = (parentId, depth) => {
    const children = menuMap.value.get(parentId) || [];
    for (const item of children) {
      list.push({ item, depth });
      if (item.kind === 'folder' && expandedIds.value.has(item.id)) {
        build(item.id, depth + 1);
      }
    }
  };
  build(0, 0);
  return list;
});

const bindItem = computed(() => {
  return normalizedMenus.value.find(m => m.kind === 'command' && isBindAction(m.name)) || null;
});

function isSelected(id) {
  return expandedIds.value.has(id);
}

function recursiveClose(parentId, set) {
  const children = menuMap.value.get(parentId) || [];
  for (const child of children) {
    set.delete(child.id);
    recursiveClose(child.id, set);
  }
}

function toggleItem(item) {
  emit('item-selected', item.raw);
  const next = new Set(expandedIds.value);
  if (next.has(item.id)) {
    next.delete(item.id);
    if (item.kind === 'folder') {
      recursiveClose(item.id, next);
    }
  } else {
    next.add(item.id);
    if (item.kind === 'folder') {
      emit('folder-open', item.raw);
    }
  }
  expandedIds.value = next;
}

function getKindIcon(kind) {
  return {
    folder: props.t.folder,
    select: props.t.select,
    command: props.t.command,
    info: props.t.info
  }[kind] || props.t.general;
}

function getSelectLabel(item) {
  return item.optionItems[item.value]?.label || `#${item.value}`;
}

function handleSelect(item, opt) {
  emit('select-change', {
    id: item.id,
    parentId: item.parentId,
    value: opt.index,
    label: opt.label,
    raw: item.raw
  });
}

function handleBind() {
  if (bindItem.value) {
    emit('bind', bindItem.value.raw);
  }
}

function handleCommand(item) {
  if (isBindAction(item.name)) {
    emit('bind', item.raw);
    return;
  }
  emit('command', item.raw);
}

function getCommandLabel(item) {
  if (isBindAction(item.name)) return props.t.startBind;
  if (item.name.toLowerCase().includes('wifi') || item.name.includes('WiFi')) return props.t.runCommand;
  return `${props.t.execute} ${item.name}`;
}

watch(normalizedMenus, (newVal) => {
  const validIds = new Set(newVal.map(m => m.id));
  const next = new Set([...expandedIds.value].filter(id => validIds.has(id)));
  if (next.size !== expandedIds.value.size) {
    expandedIds.value = next;
  }
}, { immediate: true });
</script>
