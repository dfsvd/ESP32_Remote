<template>
  <!-- Headless component -->
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue';

const props = defineProps({
  // Add props if needed
});

const emit = defineEmits(['data', 'status', 'connected']);

const isConnected = ref(false);
const socket = ref(null);
let reconnectInterval = null;

const getWsUrl = () => {
  const searchParams = new URLSearchParams(window.location.search);
  const manualHost = searchParams.get('ws') || window.localStorage.getItem('rc_ws_host');
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  
  if (manualHost) {
    return `${protocol}//${manualHost.replace(/^wss?:\/\//, '').replace(/\/ws$/, '')}/ws`;
  }
  
  const hostname = window.location.hostname;
  // If running on localhost or similar, default to typical ESP32 AP IP
  if (hostname === 'localhost' || hostname === '127.0.0.1' || hostname === '0.0.0.0') {
    return 'ws://192.168.4.1/ws';
  }
  
  return `${protocol}//${window.location.host || '192.168.4.1'}/ws`;
};

const connect = () => {
  console.log('Connecting to WebSocket...');
  try {
    socket.value = new WebSocket(getWsUrl());

    socket.value.onopen = () => {
      isConnected.value = true;
      emit('status', true);
      emit('connected');
      console.log('WebSocket connected');
      if (reconnectInterval) {
        clearInterval(reconnectInterval);
        reconnectInterval = null;
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
      if (!reconnectInterval) {
        reconnectInterval = setInterval(() => {
          connect();
        }, 3000);
      }
    };
  } catch (error) {
    console.error('Failed to create WebSocket:', error);
  }
};

const sendData = (data) => {
  if (isConnected.value && socket.value) {
    socket.value.send(data);
  }
};

defineExpose({
  sendData
});

onMounted(() => {
  connect();
});

onUnmounted(() => {
  if (reconnectInterval) clearInterval(reconnectInterval);
  if (socket.value) socket.value.close();
});
</script>
