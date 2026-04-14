/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{vue,js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        darwin: {
          bg: '#18181B',         // Very dark grey, almost black
          panel: '#27272A',      // Lighter grey for elevated cards
          amber: '#F5A623',      // Primary vibrant Amber/Yellow
          orange: '#D97706',     // Darker/burnt orange for hovers
          muted: '#A1A1AA',      // Muted text
          ink: '#FAFAFA'         // Primary white text
        }
      }
    },
  },
  plugins: [],
}
