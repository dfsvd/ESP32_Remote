<script setup>
import { ref, onMounted, onUnmounted } from 'vue';

const isConnected = ref(false);
const socket = ref(null);
let reconnectTimer = null;

const emit = defineEmits(['data', 'status', 'connected']);
defineExpose({ sendData });

function resolveWebSocketUrl() {
  const params = new URLSearchParams(window.location.search);
  const overrideHost = params.get('ws') || window.localStorage.getItem('rc_ws_host');
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  if (overrideHost) {
    const normalizedHost = overrideHost.replace(/^wss?:\/\//, '').replace(/\/ws$/, '');
    return `${protocol}//${normalizedHost}/ws`;
  }

  const hostname = window.location.hostname;
  const isLocalPreview =
    hostname === 'localhost' ||
    hostname === '127.0.0.1' ||
    hostname === '0.0.0.0';

  if (isLocalPreview) {
    return 'ws://192.168.4.1/ws';
  }

  const host = window.location.host || '192.168.4.1';
  return `${protocol}//${host}/ws`;
}

function connect() {
  console.log('Connecting to WebSocket...');
  try {
    socket.value = new WebSocket(resolveWebSocketUrl());

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
