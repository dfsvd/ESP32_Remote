<template>
  <div class="telemetry-root">
    <!-- ==================== 竖屏 / 宽屏布局 (原样保留) ==================== -->
    <div class="telemetry-portrait grid gap-4">
      <!-- 顶部状态行 -->
      <div class="grid grid-cols-2 lg:grid-cols-4 gap-3">
        <!-- 飞行模式 -->
        <article class="p-4 rounded-2xl border border-[var(--theme-border)] bg-darwin-panel">
          <span class="text-darwin-muted text-[0.65rem] uppercase tracking-widest block mb-1.5 font-bold">{{ t.flightMode }}</span>
          <div class="flex items-center gap-2">
            <span v-if="telemetry && telemetry.flightMode"
                  class="inline-flex items-center px-3 py-1 rounded-full text-sm font-bold bg-darwin-amber/10 text-darwin-amber">
              {{ telemetry.flightMode }}
            </span>
            <span v-else class="text-darwin-muted text-sm">{{ t.noData }}</span>
          </div>
        </article>

        <!-- 电池 -->
        <article class="p-4 rounded-2xl border border-[var(--theme-border)] bg-darwin-panel">
          <span class="text-darwin-muted text-[0.65rem] uppercase tracking-widest block mb-1.5 font-bold">⚡ {{ t.battery }}</span>
          <template v-if="telemetry && telemetry.battery.voltage != null">
            <div class="text-xl lg:text-2xl font-bold text-darwin-ink">{{ telemetry.battery.voltage.toFixed(1) }}V</div>
            <div class="text-xs text-darwin-muted mt-0.5">
              {{ telemetry.battery.current != null ? telemetry.battery.current.toFixed(1) + 'A' : '-.-A' }}
              · {{ telemetry.battery.remaining != null ? telemetry.battery.remaining + '%' : '--%' }}
              · {{ telemetry.battery.capacity != null ? telemetry.battery.capacity + 'mAh' : '--mAh' }}
            </div>
          </template>
          <div v-else class="text-darwin-muted text-sm">{{ t.noData }}</div>
        </article>

        <!-- GPS 状态 -->
        <article class="p-4 rounded-2xl border border-[var(--theme-border)] bg-darwin-panel">
          <span class="text-darwin-muted text-[0.65rem] uppercase tracking-widest block mb-1.5 font-bold">🛰 GPS</span>
          <template v-if="telemetry && telemetry.gps.sats > 0">
            <div class="text-xl lg:text-2xl font-bold text-green-400">{{ telemetry.gps.sats }} {{ t.sats }}</div>
            <div class="text-xs text-green-400 mt-0.5">{{ t.fixed }}</div>
          </template>
          <div v-else class="text-darwin-muted text-sm">{{ t.noGps }}</div>
        </article>

        <!-- 链路质量 -->
        <article class="p-4 rounded-2xl border border-[var(--theme-border)] bg-darwin-panel">
          <span class="text-darwin-muted text-[0.65rem] uppercase tracking-widest block mb-1.5 font-bold">{{ t.linkQuality }}</span>
          <template v-if="status && status.isLinked">
            <div class="text-xl lg:text-2xl font-bold"
              :class="{'text-green-400': status.lq >= 80, 'text-darwin-amber': status.lq >= 50 && status.lq < 80, 'text-red-400': status.lq < 50}">
              LQ {{ status.lq }}%
            </div>
            <div class="text-xs text-darwin-muted mt-0.5">RSSI {{ status.rssi }} · SNR {{ status.snr }}</div>
          </template>
          <div v-else class="text-darwin-muted text-sm">{{ t.disconnected }}</div>
        </article>
      </div>

      <!-- 姿态行 (大卡片，一行三列) -->
      <article class="p-4 lg:p-5 rounded-2xl border border-[var(--theme-border)] bg-darwin-panel">
        <h3 class="text-darwin-amber text-[0.7rem] font-bold uppercase tracking-widest mb-3">{{ t.attitude }}</h3>
        <div class="grid grid-cols-3 gap-3">
          <div class="p-3 rounded-xl border border-[var(--theme-border)] bg-[var(--theme-bg)] text-center">
            <span class="text-[0.65rem] text-darwin-muted font-bold uppercase block mb-1">Pitch</span>
            <span class="text-xl lg:text-2xl font-bold" :class="attColor(telemetry?.attitude?.pitch)">{{ telemetry ? fmtAngle(telemetry.attitude.pitch) : '--.-°' }}</span>
          </div>
          <div class="p-3 rounded-xl border border-[var(--theme-border)] bg-[var(--theme-bg)] text-center">
            <span class="text-[0.65rem] text-darwin-muted font-bold uppercase block mb-1">Roll</span>
            <span class="text-xl lg:text-2xl font-bold" :class="attColor(telemetry?.attitude?.roll)">{{ telemetry ? fmtAngle(telemetry.attitude.roll) : '--.-°' }}</span>
          </div>
          <div class="p-3 rounded-xl border border-[var(--theme-border)] bg-[var(--theme-bg)] text-center">
            <span class="text-[0.65rem] text-darwin-muted font-bold uppercase block mb-1">Yaw</span>
            <span class="text-xl lg:text-2xl font-bold text-darwin-ink">{{ telemetry ? fmtHeading(telemetry.attitude.yaw) : '--.-°' }}</span>
          </div>
        </div>
      </article>

      <!-- GPS + 气压 双列 -->
      <div class="grid grid-cols-1 lg:grid-cols-2 gap-4">
        <!-- GPS 详情 -->
        <article class="p-4 lg:p-5 rounded-2xl border border-[var(--theme-border)] bg-darwin-panel">
          <h3 class="text-darwin-amber text-[0.7rem] font-bold uppercase tracking-widest mb-3">🛰 {{ t.gpsDetail }}</h3>
          <template v-if="telemetry && telemetry.gps.latitude != null">
            <div class="text-sm font-bold text-darwin-ink mb-2 font-mono">
              <span class="text-[0.6rem] text-darwin-muted font-bold uppercase">Lat </span>{{ telemetry.gps.latitude.toFixed(6) }}
              <span class="mx-1.5 text-darwin-muted">/</span>
              <span class="text-[0.6rem] text-darwin-muted font-bold uppercase">Lon </span>{{ telemetry.gps.longitude.toFixed(6) }}
            </div>
            <div class="grid grid-cols-3 gap-2 text-center">
              <div class="p-2 rounded-lg bg-[var(--theme-bg)]">
                <div class="text-[0.6rem] text-darwin-muted font-bold uppercase">{{ t.altitude }}</div>
                <div class="text-sm font-bold text-darwin-ink">{{ telemetry.gps.altitude ? telemetry.gps.altitude + 'm' : '0m' }}</div>
              </div>
              <div class="p-2 rounded-lg bg-[var(--theme-bg)]">
                <div class="text-[0.6rem] text-darwin-muted font-bold uppercase">{{ t.speed }}</div>
                <div class="text-sm font-bold text-darwin-ink">{{ telemetry.gps.speed != null ? telemetry.gps.speed.toFixed(1) + 'm/s' : '--' }}</div>
              </div>
              <div class="p-2 rounded-lg bg-[var(--theme-bg)]">
                <div class="text-[0.6rem] text-darwin-muted font-bold uppercase">{{ t.heading }}</div>
                <div class="text-sm font-bold text-darwin-ink">{{ telemetry.gps.heading != null ? telemetry.gps.heading.toFixed(1) + '°' : '--°' }}</div>
              </div>
            </div>
          </template>
          <div v-else class="text-darwin-muted text-sm">{{ t.waitingGps }}</div>
        </article>

        <!-- 气压计 -->
        <article class="p-4 lg:p-5 rounded-2xl border border-[var(--theme-border)] bg-darwin-panel">
          <h3 class="text-darwin-amber text-[0.7rem] font-bold uppercase tracking-widest mb-3">📊 {{ t.barometer }}</h3>
          <template v-if="telemetry && telemetry.vario.altitude != null">
            <div class="flex items-end gap-6">
              <div>
                <div class="text-[0.6rem] text-darwin-muted font-bold uppercase mb-1">{{ t.baroAlt }}</div>
                <div class="text-2xl lg:text-3xl font-bold text-darwin-ink">{{ telemetry.vario.altitude.toFixed(0) }}<span class="text-sm text-darwin-muted">m</span></div>
              </div>
              <div>
                <div class="text-[0.6rem] text-darwin-muted font-bold uppercase mb-1">{{ t.vSpeed }}</div>
                <div class="text-2xl lg:text-3xl font-bold" :class="telemetry.vario.vSpeed > 0 ? 'text-green-400' : telemetry.vario.vSpeed < 0 ? 'text-red-400' : 'text-darwin-ink'">
                  {{ telemetry.vario.vSpeed >= 0 ? '+' : '' }}{{ telemetry.vario.vSpeed.toFixed(1) }}<span class="text-sm text-darwin-muted">m/s</span>
                </div>
              </div>
            </div>
          </template>
          <div v-else class="text-darwin-muted text-sm">{{ t.noData }}</div>
        </article>
      </div>

      <!-- 最后更新时间 -->
      <div class="text-[0.65rem] text-darwin-muted text-right">
        {{ t.lastUpdate }}: {{ telemetry && telemetry.lastUpdate ? formatTime(telemetry.lastUpdate) : '--:--:--' }}
      </div>
    </div>

    <!-- ==================== 横屏紧凑布局：3×2 等宽网格 ==================== -->
    <div class="telemetry-landscape">
      <div class="grid grid-cols-3 gap-2">
        <!-- 1. 飞行模式 -->
        <article class="p-3 rounded-xl border border-[var(--theme-border)] bg-darwin-panel flex flex-col justify-center">
          <span class="text-[0.6rem] text-darwin-muted uppercase tracking-wider font-bold mb-1.5">{{ t.flightMode }}</span>
          <span v-if="telemetry && telemetry.flightMode"
                class="inline-flex items-center self-start px-2.5 py-1 rounded-full text-sm font-bold bg-darwin-amber/10 text-darwin-amber">
            {{ telemetry.flightMode }}
          </span>
          <span v-else class="text-darwin-muted text-sm">{{ t.noData }}</span>
        </article>

        <!-- 2. 电池 -->
        <article class="p-3 rounded-xl border border-[var(--theme-border)] bg-darwin-panel flex flex-col justify-center">
          <span class="text-[0.6rem] text-darwin-muted uppercase tracking-wider font-bold mb-1.5">⚡ {{ t.battery }}</span>
          <template v-if="telemetry && telemetry.battery.voltage != null">
            <div class="text-xl font-bold text-darwin-ink leading-none">{{ telemetry.battery.voltage.toFixed(1) }}V</div>
            <div class="text-[0.6rem] text-darwin-muted mt-1 leading-tight">
              {{ telemetry.battery.current != null ? telemetry.battery.current.toFixed(1) + 'A' : '-.-A' }}
              · {{ telemetry.battery.remaining != null ? telemetry.battery.remaining + '%' : '--%' }}
            </div>
          </template>
          <div v-else class="text-darwin-muted text-sm">{{ t.noData }}</div>
        </article>

        <!-- 3. 链路质量 -->
        <article class="p-3 rounded-xl border border-[var(--theme-border)] bg-darwin-panel flex flex-col justify-center">
          <span class="text-[0.6rem] text-darwin-muted uppercase tracking-wider font-bold mb-1.5">{{ t.linkQuality }}</span>
          <template v-if="status && status.isLinked">
            <div class="text-xl font-bold leading-none"
              :class="{'text-green-400': status.lq >= 80, 'text-darwin-amber': status.lq >= 50 && status.lq < 80, 'text-red-400': status.lq < 50}">
              LQ {{ status.lq }}%
            </div>
            <div class="text-[0.6rem] text-darwin-muted mt-1 leading-tight">RSSI {{ status.rssi }} · SNR {{ status.snr }}</div>
          </template>
          <div v-else class="text-darwin-muted text-sm">{{ t.disconnected }}</div>
        </article>

        <!-- 4. GPS 综合 -->
        <article class="p-3 rounded-xl border border-[var(--theme-border)] bg-darwin-panel flex flex-col justify-center">
          <span class="text-[0.6rem] text-darwin-muted uppercase tracking-wider font-bold mb-1.5">🛰 GPS</span>
          <template v-if="telemetry && telemetry.gps.sats > 0">
            <div class="text-xl font-bold text-green-400 leading-none">{{ telemetry.gps.sats }} {{ t.sats }}</div>
            <div class="text-[0.6rem] text-darwin-muted mt-1 leading-tight">
              <template v-if="telemetry.gps.latitude != null">
                Lat {{ telemetry.gps.latitude.toFixed(5) }} Lon {{ telemetry.gps.longitude.toFixed(5) }}
              </template>
              <span v-if="telemetry.gps.altitude"> · {{ telemetry.gps.altitude }}m</span>
              <span v-if="telemetry.gps.speed != null"> · {{ telemetry.gps.speed.toFixed(1) }}m/s</span>
            </div>
          </template>
          <div v-else class="text-darwin-muted text-sm">{{ t.noGps }}</div>
        </article>

        <!-- 5. 姿态 -->
        <article class="p-3 rounded-xl border border-[var(--theme-border)] bg-darwin-panel flex flex-col justify-center">
          <span class="text-[0.6rem] text-darwin-muted uppercase tracking-wider font-bold mb-1.5">{{ t.attitude }}</span>
          <div class="grid grid-cols-3 gap-2 text-center">
            <div>
              <span class="text-[0.55rem] text-darwin-muted font-bold uppercase block mb-0.5">Pitch</span>
              <span class="text-base font-bold leading-tight" :class="attColor(telemetry?.attitude?.pitch)">{{ telemetry ? fmtAngle(telemetry.attitude.pitch) : '--' }}</span>
            </div>
            <div>
              <span class="text-[0.55rem] text-darwin-muted font-bold uppercase block mb-0.5">Roll</span>
              <span class="text-base font-bold leading-tight" :class="attColor(telemetry?.attitude?.roll)">{{ telemetry ? fmtAngle(telemetry.attitude.roll) : '--' }}</span>
            </div>
            <div>
              <span class="text-[0.55rem] text-darwin-muted font-bold uppercase block mb-0.5">Yaw</span>
              <span class="text-base font-bold leading-tight text-darwin-ink">{{ telemetry ? fmtHeading(telemetry.attitude.yaw) : '--' }}</span>
            </div>
          </div>
        </article>

        <!-- 6. 气压计 -->
        <article class="p-3 rounded-xl border border-[var(--theme-border)] bg-darwin-panel flex flex-col justify-center">
          <span class="text-[0.6rem] text-darwin-muted uppercase tracking-wider font-bold mb-1.5">📊 {{ t.barometer }}</span>
          <template v-if="telemetry && telemetry.vario.altitude != null">
            <div class="flex items-end gap-4">
              <div>
                <div class="text-[0.55rem] text-darwin-muted font-bold uppercase mb-0.5">{{ t.baroAlt }}</div>
                <div class="text-xl font-bold text-darwin-ink leading-none">{{ telemetry.vario.altitude.toFixed(0) }}<span class="text-[0.65rem] text-darwin-muted">m</span></div>
              </div>
              <div>
                <div class="text-[0.55rem] text-darwin-muted font-bold uppercase mb-0.5">{{ t.vSpeed }}</div>
                <div class="text-xl font-bold leading-none" :class="telemetry.vario.vSpeed > 0 ? 'text-green-400' : telemetry.vario.vSpeed < 0 ? 'text-red-400' : 'text-darwin-ink'">
                  {{ telemetry.vario.vSpeed >= 0 ? '+' : '' }}{{ telemetry.vario.vSpeed.toFixed(1) }}<span class="text-[0.65rem] text-darwin-muted">m/s</span>
                </div>
              </div>
            </div>
          </template>
          <div v-else class="text-darwin-muted text-sm">{{ t.noData }}</div>
        </article>
      </div>

      <!-- 最后更新时间 -->
      <div class="text-[0.6rem] text-darwin-muted text-right mt-2">
        {{ t.lastUpdate }}: {{ telemetry && telemetry.lastUpdate ? formatTime(telemetry.lastUpdate) : '--:--:--' }}
      </div>
    </div>
  </div>
</template>

<script setup>
defineProps({
  telemetry: { type: Object, default: null },
  status: { type: Object, default: () => ({}) },
  t: { type: Object, required: true },
})

function fmtAngle(deg) {
  if (deg == null) return '--.-°'
  const sign = deg >= 0 ? '+' : ''
  return sign + deg.toFixed(1) + '°'
}

function fmtHeading(deg) {
  if (deg == null) return '--.-°'
  return deg.toFixed(1) + '°'
}

function attColor(deg) {
  if (deg == null) return 'text-darwin-muted'
  const abs = Math.abs(deg)
  if (abs > 80) return 'text-red-400'
  if (abs > 45) return 'text-darwin-amber'
  return 'text-darwin-ink'
}

function formatTime(ts) {
  const d = new Date(ts)
  return d.toLocaleTimeString()
}
</script>

<style scoped>
/* 竖屏版默认显示 */
.telemetry-landscape { display: none; }
.telemetry-portrait { display: grid; }

/* 手机横屏：填满可用高度，网格等分 */
@media (orientation: landscape) and (max-height: 600px) {
  .telemetry-landscape {
    display: flex;
    flex-direction: column;
    flex: 1;
  }
  .telemetry-landscape > :first-child {
    flex: 1;
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    grid-template-rows: 1fr 1fr;
    gap: 0.5rem;
  }
  .telemetry-portrait { display: none; }
}
</style>
