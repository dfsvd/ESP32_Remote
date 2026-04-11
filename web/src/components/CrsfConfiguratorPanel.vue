<template>
  <section class="crsf-configurator">
    <header class="toolbar-strip">
      <div class="hero-actions">
        <button class="ghost-btn" type="button" @click="$emit('refresh')">
          刷新
        </button>
        <button
          class="primary-btn"
          type="button"
          :disabled="loading || !bindItem"
          @click="triggerBind"
        >
          {{ loading ? "处理中..." : "开始对频" }}
        </button>
      </div>

      <section class="status-strip">
        <article class="stat-card">
          <span class="stat-label">链路</span>
          <strong :class="['stat-value', status.isLinked ? 'ok' : 'muted']">
            {{ status.isLinked ? "已连接" : "离线" }}
          </strong>
        </article>

        <article class="stat-card">
          <span class="stat-label">信号</span>
          <strong class="stat-value">{{ status.rssi ?? "--" }}</strong>
        </article>

        <article class="stat-card">
          <span class="stat-label">菜单同步</span>
          <strong class="stat-value">
            {{ status.loadedParams ?? 0 }} / {{ status.totalParams ?? 0 }}
          </strong>
        </article>
      </section>
    </header>

    <section class="workspace">
      <aside class="browser">
        <ul class="menu-list">
          <li
            v-for="entry in visibleItems"
            :key="entry.item.id"
            class="menu-entry"
            :style="{ '--menu-depth': entry.depth }"
          >
            <button
              type="button"
              class="menu-item"
              :class="{
                expanded: isExpanded(entry.item.id),
                folder: entry.item.kind === 'folder',
                nested: entry.depth > 0,
              }"
              @click="toggleItem(entry.item)"
            >
              <span class="menu-meta">
                <span class="menu-icon">{{ kindIcon(entry.item.kind) }}</span>
                <span class="menu-copy">
                  <strong>{{ entry.item.name }}</strong>
                  <small>#{{ entry.item.id }} · {{ entry.item.kindLabel }}</small>
                </span>
              </span>

              <span class="menu-tail">
                <span v-if="entry.item.kind === 'select'" class="pill">
                  {{ displayCurrentOption(entry.item) }}
                </span>
                <span v-else-if="entry.item.kind === 'info'" class="pill subtle">
                  只读
                </span>
                <span v-else-if="entry.item.kind === 'command'" class="pill warn">
                  命令
                </span>
                <span
                  class="arrow"
                  :class="{ expanded: isExpanded(entry.item.id) }"
                >
                  ▾
                </span>
              </span>
            </button>

            <section
              v-if="isExpanded(entry.item.id) && entry.item.kind !== 'folder'"
              class="menu-inline-detail"
            >
              <section v-if="entry.item.kind === 'select'" class="detail-card inline-card">
                <div class="option-grid">
                  <button
                    v-for="option in entry.item.optionItems"
                    :key="`${entry.item.id}-${option.index}`"
                    type="button"
                    class="option-card"
                    :class="{ selected: option.index === entry.item.value }"
                    @click.stop="emitSelectChange(entry.item, option)"
                  >
                    <span class="option-index">{{ option.index }}</span>
                    <strong>{{ option.label }}</strong>
                  </button>
                </div>
              </section>

              <section v-else-if="entry.item.kind === 'command'" class="detail-card inline-card">
                <div class="command-actions">
                  <button
                    class="primary-btn"
                    type="button"
                    :disabled="loading"
                    @click.stop="triggerCommand(entry.item)"
                  >
                    {{ commandLabel(entry.item) }}
                  </button>
                </div>
              </section>

              <section v-else-if="entry.item.kind === 'info'" class="detail-card inline-card">
                <div class="info-block">{{ entry.item.content || "-" }}</div>
              </section>

              <section v-else class="detail-card inline-card">
                <div class="info-block">{{ entry.item.content || "No preview available." }}</div>
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
    return rawOptions.map((label, index) => ({ index, label }));
  }
  if (!rawOptions) return [];
  return String(rawOptions)
    .split(";")
    .map((label, index) => ({ index, label: label.trim() }))
    .filter((item) => item.label.length > 0);
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

<style scoped>
.crsf-configurator {
  --bg: #f4f1ea;
  --panel: rgba(255, 250, 242, 0.9);
  --panel-strong: #fffdf8;
  --ink: #182126;
  --muted: #627078;
  --line: rgba(24, 33, 38, 0.1);
  --accent: #0f7b78;
  --accent-strong: #0c6461;
  --warn: #cb7a2f;
  --glow: rgba(15, 123, 120, 0.16);
  display: grid;
  gap: 18px;
  padding: 24px;
  border-radius: 28px;
  color: var(--ink);
  background:
    radial-gradient(circle at top left, rgba(15, 123, 120, 0.18), transparent 35%),
    radial-gradient(circle at top right, rgba(203, 122, 47, 0.16), transparent 28%),
    linear-gradient(180deg, #fcfbf7 0%, var(--bg) 100%);
  box-shadow: 0 28px 80px rgba(42, 54, 65, 0.12);
}

.toolbar-strip,
.workspace,
.browser,
.detail-card {
  border: 1px solid var(--line);
}

.toolbar-strip {
  display: flex;
  align-items: stretch;
  justify-content: space-between;
  gap: 16px;
  padding: 18px;
  border-radius: 24px;
  background: linear-gradient(135deg, rgba(255, 255, 255, 0.85), rgba(247, 241, 231, 0.94));
}

.hero-actions,
.command-actions {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}

.status-strip {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 14px;
  flex: 1 1 auto;
}

.stat-card {
  padding: 18px;
  border-radius: 18px;
  background: var(--panel-strong);
}

.stat-label {
  display: block;
  margin-bottom: 8px;
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: 0.16em;
  color: var(--muted);
}

.stat-value {
  display: block;
  font-size: 26px;
}

.stat-value.ok {
  color: var(--accent-strong);
}

.stat-value.muted {
  color: #8b6f5d;
}

.workspace {
  padding: 18px;
  border-radius: 26px;
  background: rgba(255, 255, 255, 0.55);
}

.browser {
  padding: 18px;
  border-radius: 22px;
  background: var(--panel);
}

.ghost-btn,
.primary-btn,
.menu-item,
.option-card {
  transition:
    transform 160ms ease,
    box-shadow 160ms ease,
    border-color 160ms ease,
    background 160ms ease;
}

.ghost-btn {
  border: 1px solid var(--line);
  background: white;
  color: var(--ink);
}

.ghost-btn,
.primary-btn {
  padding: 12px 18px;
  border-radius: 999px;
  cursor: pointer;
  font-weight: 700;
}

.primary-btn {
  border: 1px solid transparent;
  background: linear-gradient(135deg, var(--accent), var(--accent-strong));
  color: white;
  box-shadow: 0 16px 30px var(--glow);
}

.primary-btn:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.menu-list {
  display: grid;
  gap: 10px;
  padding: 0;
  margin: 0;
  list-style: none;
}

.menu-entry {
  display: grid;
  gap: 0;
}

.menu-item {
  width: 100%;
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto;
  align-items: center;
  gap: 14px;
  text-align: left;
  padding: 14px;
  padding-left: calc(14px + (var(--menu-depth) * 18px));
  border-radius: 18px;
  border: 1px solid transparent;
  background: rgba(255, 255, 255, 0.84);
  cursor: pointer;
}

.menu-item.expanded,
.menu-item:hover,
.option-card:hover,
.ghost-btn:hover,
.primary-btn:hover {
  transform: translateY(-1px);
  border-color: rgba(15, 123, 120, 0.3);
  box-shadow: 0 14px 26px rgba(24, 33, 38, 0.08);
}

.menu-item.nested {
  background: rgba(252, 249, 243, 0.92);
}

.menu-item.folder.expanded {
  background: linear-gradient(180deg, rgba(15, 123, 120, 0.08), rgba(255, 255, 255, 0.95));
}

.menu-meta {
  display: flex;
  align-items: center;
  gap: 12px;
  min-width: 0;
}

.menu-copy {
  min-width: 0;
  display: grid;
  align-content: center;
}

.menu-meta strong,
.option-card strong {
  display: block;
  line-height: 1.2;
  word-break: break-word;
}

.menu-meta small {
  display: block;
  margin-top: 4px;
  font-size: 12px;
}

.menu-icon {
  width: 38px;
  height: 38px;
  display: grid;
  place-items: center;
  border-radius: 12px;
  background: rgba(15, 123, 120, 0.1);
  color: var(--accent-strong);
  font-size: 12px;
  font-weight: 700;
  flex-shrink: 0;
}

.menu-tail {
  display: flex;
  align-items: center;
  align-self: center;
  justify-self: end;
  gap: 8px;
  flex-shrink: 0;
}

.pill {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  padding: 7px 10px;
  border-radius: 999px;
  font-size: 12px;
  font-weight: 700;
  white-space: nowrap;
  background: rgba(15, 123, 120, 0.1);
  color: var(--accent-strong);
}

.pill.subtle {
  background: rgba(98, 112, 120, 0.12);
  color: #40525b;
}

.pill.warn {
  background: rgba(203, 122, 47, 0.16);
  color: #9a5d1f;
}

.arrow {
  font-size: 18px;
  color: var(--accent-strong);
  transform: rotate(-90deg);
  transition: transform 160ms ease;
}

.arrow.expanded {
  transform: rotate(0deg);
}

.menu-inline-detail {
  display: grid;
  gap: 10px;
  padding-left: calc(14px + (var(--menu-depth) * 18px));
}

.detail-card {
  padding: 16px;
  margin-top: 10px;
  border-radius: 18px;
  background: var(--panel-strong);
}

.option-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
  gap: 12px;
}

.option-card {
  min-height: 90px;
  display: grid;
  align-content: space-between;
  padding: 14px;
  border-radius: 18px;
  border: 1px solid var(--line);
  background: rgba(248, 244, 236, 0.86);
  text-align: left;
  cursor: pointer;
}

.option-card.selected {
  border-color: rgba(15, 123, 120, 0.45);
  background: linear-gradient(180deg, rgba(15, 123, 120, 0.08), rgba(255, 255, 255, 0.98));
}

.option-index {
  display: inline-flex;
  width: fit-content;
  padding: 4px 8px;
  border-radius: 999px;
  background: rgba(24, 33, 38, 0.08);
  font-size: 11px;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}

.info-block {
  padding: 16px;
  border-radius: 16px;
  background: #f5f1e7;
  border: 1px solid rgba(24, 33, 38, 0.08);
  color: #2f3b42;
  font-family: "SFMono-Regular", Consolas, "Liberation Mono", Menlo, monospace;
  word-break: break-word;
}

@media (max-width: 960px) {
  .toolbar-strip {
    display: grid;
    gap: 12px;
  }

  .status-strip {
    grid-template-columns: repeat(3, minmax(0, 1fr));
    gap: 10px;
  }

  .workspace {
    padding: 14px;
  }

  .stat-card {
    padding: 14px 10px;
    text-align: center;
  }

  .stat-label {
    margin-bottom: 6px;
    font-size: 11px;
    letter-spacing: 0.12em;
  }

  .stat-value {
    font-size: 18px;
    line-height: 1.15;
  }

  .menu-item {
    gap: 10px;
    padding: 13px 12px;
    padding-left: calc(12px + (var(--menu-depth) * 14px));
  }

  .menu-inline-detail {
    padding-left: calc(12px + (var(--menu-depth) * 14px));
  }

  .menu-icon {
    width: 34px;
    height: 34px;
    border-radius: 10px;
  }

  .pill {
    max-width: 100%;
    padding: 6px 10px;
    font-size: 11px;
  }

  .option-grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}
</style>
