<script setup>
import { ref, onMounted, onUnmounted } from 'vue';

const isConnected = ref(false);
const socket = ref(null);
// const wsUrl = `ws://${window.location.hostname}/ws`; // Hardcoded URL
const wsUrl = 'ws://192.168.4.1/ws';
let reconnectTimer = null;

const emit = defineEmits(['data', 'status', 'connected']);
defineExpose({ sendData });

function connect() {
  console.log('Connecting to WebSocket...');
  try {
    socket.value = new WebSocket(wsUrl);

    socket.value.onopen = () => {
      isConnected.value = true;
      emit('status', true);
      emit('connected');
      console.log('WebSocket connected');
      if (reconnectTimer) {
        clearInterval(reconnectTimer);
        reconnectTimer = null;
      }
    };

    socket.value.onmessage = (event) => {
      emit('data', event.data);
    };

    socket.value.onerror = (error) => {
      console.error('WebSocket Error:', error);
    };

    socket.value.onclose = () => {
      isConnected.value = false;
      emit('status', false);
      socket.value = null;
      console.log('WebSocket disconnected. Attempting to reconnect...');
      // Setup reconnect interval if not already running
      if (!reconnectTimer) {
        reconnectTimer = setInterval(() => {
          connect();
        }, 3000); // Try to reconnect every 3 seconds
      }
    };
  } catch (error) {
    console.error('Failed to create WebSocket:', error);
  }
}

function sendData(data) {
  if (isConnected.value && socket.value) {
    socket.value.send(data);
  } else {
    // console.warn('Cannot send data, WebSocket is not connected.');
  }
}

onMounted(connect);

onUnmounted(() => {
  if (reconnectTimer) {
    clearInterval(reconnectTimer);
  }
  if (socket.value) {
    socket.value.close();
  }
});
</script>

// No template, this is a headless component.
