<template>
  <section class="grid gap-[18px] p-6 rounded-[28px] text-darwin-ink bg-[radial-gradient(circle_at_top_left,rgba(245,166,35,0.1),transparent_35%),radial-gradient(circle_at_top_right,rgba(217,119,6,0.1),transparent_28%),linear-gradient(180deg,rgba(45,45,45,0.9),rgba(35,35,35,0.9))] shadow-[0_28px_80px_rgba(0,0,0,0.4)] border border-white/5">
    <header class="flex flex-col lg:flex-row items-stretch justify-between gap-4 p-4 lg:p-[18px] rounded-[24px] bg-gradient-to-br from-white/10 to-transparent border border-white/10">
      <div class="flex items-center gap-3 flex-wrap">
        <button class="px-[18px] py-[12px] bg-white/10 border border-white/10 text-darwin-ink font-bold rounded-full cursor-pointer transition-all hover:-translate-y-px hover:bg-white/20" type="button" @click="$emit('refresh')">
          刷新
        </button>
        <button
          class="px-[18px] py-[12px] border border-transparent bg-gradient-to-br from-darwin-amber to-darwin-orange text-black font-bold rounded-full shadow-[0_16px_30px_rgba(245,166,35,0.2)] cursor-pointer transition-all hover:-translate-y-px disabled:opacity-60 disabled:cursor-not-allowed"
          type="button"
          :disabled="loading || !bindItem"
          @click="triggerBind"
        >
          {{ loading ? "处理中..." : "开始对频" }}
        </button>
      </div>

      <section class="grid grid-cols-3 gap-2.5 lg:gap-[14px] flex-auto">
        <article class="p-3.5 lg:p-[18px] rounded-[18px] text-center lg:text-left bg-[#1f1f22] border border-white/5">
          <span class="block mb-1.5 lg:mb-2 text-[11px] lg:text-xs uppercase tracking-[0.16em] text-darwin-muted">链路</span>
          <strong class="block text-[18px] lg:text-[26px] leading-[1.15]" :class="status.isLinked ? 'text-darwin-amber' : 'text-[#8b6f5d]'">
            {{ status.isLinked ? "已连接" : "离线" }}
          </strong>
        </article>

        <article class="p-3.5 lg:p-[18px] rounded-[18px] text-center lg:text-left bg-[#1f1f22] border border-white/5">
          <span class="block mb-1.5 lg:mb-2 text-[11px] lg:text-xs uppercase tracking-[0.16em] text-darwin-muted">信号</span>
          <strong class="block text-[18px] lg:text-[26px] leading-[1.15] text-darwin-ink">{{ status.rssi ?? "--" }}</strong>
        </article>

        <article class="p-3.5 lg:p-[18px] rounded-[18px] text-center lg:text-left bg-[#1f1f22] border border-white/5">
          <span class="block mb-1.5 lg:mb-2 text-[11px] lg:text-xs uppercase tracking-[0.16em] text-darwin-muted">菜单同步</span>
          <strong class="block text-[18px] lg:text-[26px] leading-[1.15] text-darwin-ink">
            {{ status.loadedParams ?? 0 }} / {{ status.totalParams ?? 0 }}
          </strong>
        </article>
      </section>
    </header>

    <section class="p-3.5 lg:p-[18px] rounded-[26px] bg-white/5 border border-white/10">
      <aside class="p-3.5 lg:p-[18px] rounded-[22px] bg-[#1a1a1c] border border-white/5">
        <ul class="grid gap-2.5 p-0 m-0 list-none">
          <li
            v-for="entry in visibleItems"
            :key="entry.item.id"
            class="grid gap-0"
            :style="{ '--menu-depth': entry.depth }"
          >
            <button
              type="button"
              class="w-full grid grid-cols-[minmax(0,1fr)_auto] items-center gap-[10px] lg:gap-[14px] text-left p-[13px_12px] lg:p-[14px] rounded-[18px] border border-transparent bg-white/5 text-darwin-ink cursor-pointer transition-all hover:-translate-y-px hover:border-darwin-amber/30 hover:shadow-[0_14px_26px_rgba(0,0,0,0.5)]"
              :class="{
                '!-translate-y-px !border-darwin-amber/30 !shadow-[0_14px_26px_rgba(0,0,0,0.5)] bg-gradient-to-b from-darwin-amber/10 to-transparent': isExpanded(entry.item.id),
                'bg-gradient-to-b from-white/10 to-white/5': entry.item.kind === 'folder' && isExpanded(entry.item.id),
                '!bg-white/10': entry.depth > 0,
              }"
              :style="{ paddingLeft: `calc(12px + (${entry.depth} * 18px))` }"
              @click="toggleItem(entry.item)"
            >
              <span class="flex items-center gap-3 min-w-0">
                <span class="flex-shrink-0 grid place-items-center w-[34px] h-[34px] lg:w-[38px] lg:h-[38px] rounded-[10px] lg:rounded-[12px] bg-darwin-amber/10 text-darwin-amber text-xs font-bold">{{ kindIcon(entry.item.kind) }}</span>
                <span class="grid align-content-center min-w-0">
                  <strong class="block leading-[1.2] break-words">{{ entry.item.name }}</strong>
                  <small class="block mt-1 text-xs text-darwin-muted">#{{ entry.item.id }} · {{ entry.item.kindLabel }}</small>
                </span>
              </span>

              <span class="flex items-center self-center justify-self-end gap-2 flex-shrink-0">
                <span v-if="entry.item.kind === 'select'" class="inline-flex items-center justify-center px-2.5 py-1.5 rounded-full text-[11px] lg:text-xs font-bold whitespace-nowrap bg-darwin-amber/10 text-darwin-amber max-w-full">
                  {{ displayCurrentOption(entry.item) }}
                </span>
                <span v-else-if="entry.item.kind === 'info'" class="inline-flex items-center justify-center px-2.5 py-1.5 rounded-full text-[11px] lg:text-xs font-bold whitespace-nowrap bg-white/10 text-darwin-muted max-w-full">
                  只读
                </span>
                <span v-else-if="entry.item.kind === 'command'" class="inline-flex items-center justify-center px-2.5 py-1.5 rounded-full text-[11px] lg:text-xs font-bold whitespace-nowrap bg-[#cb7a2f]/20 text-[#d97706] max-w-full">
                  命令
                </span>
                <span
                  class="text-[18px] text-darwin-amber transition-transform duration-160"
                  :class="{ 'rotate-90': isExpanded(entry.item.id) }"
                  style="transform: rotate(-90deg);"
                  :style="{ transform: isExpanded(entry.item.id) ? 'rotate(0deg)' : 'rotate(-90deg)' }"
                >
                  ▾
                </span>
              </span>
            </button>

            <section
              v-if="isExpanded(entry.item.id) && entry.item.kind !== 'folder'"
              class="grid gap-2.5"
              :style="{ paddingLeft: `calc(12px + (${entry.depth} * 18px))` }"
            >
              <section v-if="entry.item.kind === 'select'" class="p-4 mt-2.5 border border-white/5 rounded-[18px] bg-[#1f1f22]">
                <div class="grid grid-cols-2 sm:grid-cols-[repeat(auto-fit,minmax(140px,1fr))] gap-3">
                  <button
                    v-for="option in entry.item.optionItems"
                    :key="`${entry.item.id}-${option.index}`"
                    type="button"
                    class="min-h-[90px] grid content-between p-3.5 rounded-[18px] border border-white/10 bg-white/5 text-left cursor-pointer text-darwin-ink transition-all hover:-translate-y-px hover:border-darwin-amber/40 hover:shadow-[0_14px_26px_rgba(0,0,0,0.4)]"
                    :class="{ 'border-darwin-amber/50 bg-gradient-to-b from-darwin-amber/10 to-transparent shadow-[0_14px_26px_rgba(0,0,0,0.4)]': option.index === entry.item.value }"
                    @click.stop="emitSelectChange(entry.item, option)"
                  >
                    <span class="inline-flex w-fit px-2 py-1 rounded-full bg-black/20 text-[11px] tracking-[0.08em] uppercase text-darwin-muted">{{ option.index }}</span>
                    <strong class="block leading-[1.2] break-words">{{ option.label }}</strong>
                  </button>
                </div>
              </section>

              <section v-else-if="entry.item.kind === 'command'" class="p-4 mt-2.5 border border-white/5 rounded-[18px] bg-[#1f1f22]">
                <div class="flex items-center gap-3 flex-wrap">
                  <button
                    class="px-[18px] py-[12px] border border-transparent bg-gradient-to-br from-darwin-amber to-darwin-orange text-black font-bold rounded-full shadow-[0_16px_30px_rgba(245,166,35,0.2)] cursor-pointer transition-all hover:-translate-y-px disabled:opacity-60 disabled:cursor-not-allowed"
                    type="button"
                    :disabled="loading"
                    @click.stop="triggerCommand(entry.item)"
                  >
                    {{ commandLabel(entry.item) }}
                  </button>
                </div>
              </section>

              <section v-else-if="entry.item.kind === 'info'" class="p-4 mt-2.5 border border-white/5 rounded-[18px] bg-[#1f1f22]">
                <div class="p-4 rounded-[16px] bg-black/30 border border-white/5 text-darwin-muted font-mono break-words">{{ entry.item.content || "-" }}</div>
              </section>

              <section v-else class="p-4 mt-2.5 border border-white/5 rounded-[18px] bg-[#1f1f22]">
                <div class="p-4 rounded-[16px] bg-black/30 border border-white/5 text-darwin-muted font-mono break-words">{{ entry.item.content || "No preview available." }}</div>
              </section>
            </section>
          </li>
        </ul>
      </aside>
    </section>
  </section>
</template>

<script setup>
import { computed, ref, watch } from "vue";

const props = defineProps({
  title: {
    type: String,
    default: "",
  },
  subtitle: {
    type: String,
    default: "",
  },
  description: {
    type: String,
    default:
      "A reusable CRSF menu panel for Vue apps. Feed it with your own WebSocket state and handle outgoing commands in the parent page.",
  },
  loading: {
    type: Boolean,
    default: false,
  },
  status: {
    type: Object,
    default: () => ({
      isReady: false,
      isLinked: false,
      rssi: null,
      lq: null,
      loadedParams: 0,
      totalParams: 0,
      deviceLabel: "",
    }),
  },
  menus: {
    type: Array,
    default: () => [],
  },
});

const emit = defineEmits([
  "refresh",
  "bind",
  "command",
  "select-change",
  "item-selected",
  "folder-open",
]);

function normalizeType(type) {
  if (typeof type === "string") return type;
  switch (type) {
    case 0x09:
      return "select";
    case 0x0b:
      return "folder";
    case 0x0c:
      return "info";
    case 0x0d:
      return "command";
    case 0x0a:
      return "text";
    default:
      return "unknown";
  }
}

function kindLabel(kind) {
  switch (kind) {
    case "select":
      return "选项";
    case "folder":
      return "目录";
    case "command":
      return "命令";
    case "info":
      return "信息";
    case "text":
      return "文本";
    default:
      return "通用";
  }
}

function optionItems(rawOptions) {
  if (Array.isArray(rawOptions)) {
    return rawOptions.map((label, index) => ({
      index,
      rawLabel: String(label ?? "").trim(),
      label: compactOptionLabel(label),
    }));
  }
  if (!rawOptions) return [];
  return String(rawOptions)
    .split(";")
    .map((label, index) => ({
      index,
      rawLabel: label.trim(),
      label: compactOptionLabel(label),
    }))
    .filter((item) => item.label.length > 0);
}

function compactOptionLabel(label) {
  const text = String(label ?? "").trim();
  if (!text) return "";
  const withoutDbm = text.replace(/\s*\([^)]*\)\s*$/u, "").trim();
  return withoutDbm || text;
}

function isBindLike(name = "") {
  const normalizedName = String(name).toLowerCase();
  return normalizedName.includes("bind") || normalizedName.includes("对频");
}

const normalizedMenus = computed(() =>
  props.menus
    .map((item) => {
      const kind = normalizeType(item.type ?? item.kind);
      return {
        id: item.id,
        parentId: item.parentId ?? item.parent_id ?? 0,
        name: item.name || `Item ${item.id}`,
        kind,
        kindLabel: kindLabel(kind),
        value: Number.isFinite(item.value) ? item.value : 0,
        content: item.content ?? item.options ?? "",
        optionItems: optionItems(item.options),
        raw: item,
      };
    })
    .sort((a, b) => a.id - b.id)
);

const byParent = computed(() => {
  const groups = new Map();
  for (const item of normalizedMenus.value) {
    if (!groups.has(item.parentId)) groups.set(item.parentId, []);
    groups.get(item.parentId).push(item);
  }
  return groups;
});

const expandedIds = ref(new Set());

const bindItem = computed(() =>
  normalizedMenus.value.find((item) => item.kind === "command" && isBindLike(item.name)) || null
);

const visibleItems = computed(() => {
  const flattened = [];

  const appendChildren = (parentId, depth) => {
    const items = byParent.value.get(parentId) || [];
    for (const item of items) {
      flattened.push({ item, depth });
      if (item.kind === "folder" && expandedIds.value.has(item.id)) {
        appendChildren(item.id, depth + 1);
      }
    }
  };

  appendChildren(0, 0);
  return flattened;
});

function isExpanded(id) {
  return expandedIds.value.has(id);
}

function collapseDescendants(parentId, next) {
  const children = byParent.value.get(parentId) || [];
  for (const child of children) {
    next.delete(child.id);
    collapseDescendants(child.id, next);
  }
}

function toggleItem(item) {
  emit("item-selected", item.raw);

  const next = new Set(expandedIds.value);
  if (next.has(item.id)) {
    next.delete(item.id);
    if (item.kind === "folder") {
      collapseDescendants(item.id, next);
    }
  } else {
    next.add(item.id);
    if (item.kind === "folder") {
      emit("folder-open", item.raw);
    }
  }

  expandedIds.value = next;
}

function kindIcon(kind) {
  switch (kind) {
    case "folder":
      return "目录";
    case "select":
      return "选项";
    case "command":
      return "命令";
    case "info":
      return "信息";
    default:
      return "通用";
  }
}

function displayCurrentOption(item) {
  return item.optionItems[item.value]?.label || `#${item.value}`;
}

function emitSelectChange(item, option) {
  emit("select-change", {
    id: item.id,
    parentId: item.parentId,
    value: option.index,
    label: option.label,
    raw: item.raw,
  });
}

function triggerBind() {
  if (!bindItem.value) return;
  emit("bind", bindItem.value.raw);
}

function triggerCommand(item) {
  if (isBindLike(item.name)) {
    emit("bind", item.raw);
    return;
  }
  emit("command", item.raw);
}

function commandLabel(item) {
  if (isBindLike(item.name)) return "开始对频";
  if (item.name.toLowerCase().includes("wifi") || item.name.includes("WiFi")) return "执行命令";
  return `执行 ${item.name}`;
}

watch(
  normalizedMenus,
  (items) => {
    const validIds = new Set(items.map((item) => item.id));
    const next = new Set([...expandedIds.value].filter((id) => validIds.has(id)));
    if (next.size !== expandedIds.value.size) {
      expandedIds.value = next;
    }
  },
  { immediate: true }
);
</script>
