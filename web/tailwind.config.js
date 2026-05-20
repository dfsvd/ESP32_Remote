/** @type {import('tailwindcss').Config} */
export default {
  darkMode: 'class',
  content: [
    "./index.html",
    "./src/**/*.{vue,js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        darwin: {
          bg: 'var(--theme-bg)',
          panel: 'var(--theme-panel)',
          amber: '#F5A623',
          orange: '#D97706',
          muted: 'var(--theme-text-muted)',
          ink: 'var(--theme-text)'
        },
        theme: {
          subtle: 'var(--theme-bg-subtle)',
          hover: 'var(--theme-bg-hover)',
          active: 'var(--theme-bg-active)',
          border: 'var(--theme-border)',
          'border-light': 'var(--theme-border-light)',
          input: 'var(--theme-input-bg)',
          'input-border': 'var(--theme-input-border)',
          'input-text': 'var(--theme-input-text)',
          'panel-alt': 'var(--theme-panel-alt)',
          secondary: 'var(--theme-bg-secondary)',
          header: 'var(--theme-header-bg)',
          'offline-text': 'var(--theme-offline-text)',
          'cmd-badge-bg': 'var(--theme-cmd-badge-bg)',
          'cmd-badge-text': 'var(--theme-cmd-badge-text)',
        }
      }
    },
  },
  plugins: [],
}
