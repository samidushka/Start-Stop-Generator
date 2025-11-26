// Конфигурация подключения
const WS_CONFIG = {
  host: window.location.hostname,
  ports: [window.location.port || 80, 8080],
  path: '/ws',
  reconnectDelay: 2000,
  maxReconnectAttempts: 5
};

// Состояние подключения
let webSocket = null;
let currentPortIndex = 0;
let reconnectAttempts = 0;
let isConnected = false;
let connectionCheckInterval = null;
let currentDisplayMode = 'values'; // 'values' или 'charts'
let currentChartRange = '24h'; // '24h' или '1h'
let sensorCharts = {};
console.log('values-mode element:', document.getElementById('values-mode'));
console.log('chart-mode element:', document.getElementById('chart-mode'));
// Глобальные переменные для пагинации
let currentPage = 0;
const PAGE_SIZE = 50;
let allLogLines = [];

// Инициализация при загрузке страницы
window.addEventListener('load', () => {
  initWebSocket();
  startConnectionMonitor();
  updateDateTime();
  setInterval(updateDateTime, 1000);
  //initCharts();
  //setupModeSwitcher();
});

// Инициализация WebSocket соединения
function initWebSocket() {
  if (webSocket) {
    webSocket.onopen = null;
    webSocket.onclose = null;
    webSocket.onmessage = null;
    webSocket.close();
  }

  const port = WS_CONFIG.ports[currentPortIndex];
  const wsUrl = `ws://${WS_CONFIG.host}:${port}${WS_CONFIG.path}`;
  
  console.log(`Попытка подключения к ${wsUrl}`);
  webSocket = new WebSocket(wsUrl);

  webSocket.onopen = () => {
    console.log(`Успешное подключение к порту ${port}`);
    isConnected = true;
    reconnectAttempts = 0;
    onConnectionEstablished();
  };

  webSocket.onclose = () => {
    console.log(`Соединение закрыто (порт ${port})`);
    isConnected = false;
    handleDisconnection();
  };

  webSocket.onerror = (error) => {
    console.error(`Ошибка подключения (порт ${port}):`, error);
  };

  webSocket.onmessage = onWebSocketMessage;
}

// Обработка разрыва соединения
function handleDisconnection() {
  if (reconnectAttempts < WS_CONFIG.maxReconnectAttempts) {
    reconnectAttempts++;
    console.log(`Повторная попытка #${reconnectAttempts} через ${WS_CONFIG.reconnectDelay/1000} сек`);
    setTimeout(initWebSocket, WS_CONFIG.reconnectDelay);
  } else {
    currentPortIndex = (currentPortIndex + 1) % WS_CONFIG.ports.length;
    reconnectAttempts = 0;
    console.log(`Переход на порт ${WS_CONFIG.ports[currentPortIndex]}`);
    setTimeout(initWebSocket, WS_CONFIG.reconnectDelay);
  }
}

// Мониторинг соединения
function startConnectionMonitor() {
  if (connectionCheckInterval) clearInterval(connectionCheckInterval);
  connectionCheckInterval = setInterval(() => {
    if (!isConnected || !webSocket || webSocket.readyState !== WebSocket.OPEN) {
      console.log('Монитор: соединение разорвано, переподключаемся...');
      initWebSocket();
    }
  }, 10000);
}

// Действия после установки соединения
function onConnectionEstablished() {
  sendWebSocketCommand('states');
  loadEventLog();
}

// Отправка команд через WebSocket
function sendWebSocketCommand(command) {
  if (command === undefined || command.length > 50) { // Пример валидации
    console.error("Invalid command:", command);
    return;
  }
  if (isConnected && webSocket && webSocket.readyState === WebSocket.OPEN) {
    webSocket.send(command);
  } else {
    console.warn('Попытка отправить команду при отсутствующем соединении');
  }
}

// Обработка входящих сообщений
function onWebSocketMessage(event) {
  try {
    const data = JSON.parse(event.data);
    //console.log('Получены данные:', data);
    updateUI(data);
  } catch (error) {
    console.error('Ошибка обработки данных:', error);
  }
}

// Обновление интерфейса
function updateUI(data) {
  //console.log('Updating UI with data:', data);
  // Обновление напряжения
  updateVoltageCard('networkVoltage', data.networkVoltage);
  updateVoltageCard('generatorVoltage', data.generatorVoltage);
  
  // Обновление состояния генератора
  const generatorState = data.generatorState === '1' ? '🟢 Запущен' : '🔴 Остановлен';
  updateElement('generatorState', generatorState);
    // Добавляем/удаляем класс running
    const controlContainer = document.querySelector('.control-container');
    if (data.generatorState === '1') {
        controlContainer.classList.add('running');
    } else {
        controlContainer.classList.remove('running');
    }
  // Обновление GPIO
  if (data.gpios && Array.isArray(data.gpios)) {
    data.gpios.forEach(gpio => {
      const element = document.getElementById(gpio.output);
      if (element) {
        element.checked = gpio.state === '1';
        updateElement(`${gpio.output}s`, gpio.state === '1' ? 'ON' : 'OFF');
      }
    });
  }
  
  // Обновление данных датчиков
  updateSensorData(1, data.temperature1, data.humidity1, data.pressure1);
  updateSensorData(2, data.temperature2, data.humidity2, data.pressure2);
  
  //if (currentDisplayMode === 'charts') {
  //  loadChartData();
  //}
}

// Вспомогательная функция для обновления элементов
function updateElement(id, value, suffix = '') {
  const element = document.getElementById(id);
  if (element) {
    element.innerText = value !== undefined ? `${value}${suffix}` : `--${suffix}`;
  }
}

// Обновление данных датчиков
function updateSensorData(sensorNum, temp, hum, press) {
  const formatValue = (value) => {
    if (value === null || value === undefined || value === "nan" || isNaN(value)) {
        return "--";
    }
    return value;
  };
  updateElement(`temperature${sensorNum}`, temp, ' °C');
  updateElement(`humidity${sensorNum}`, hum, ' %');
  updateElement(`pressure${sensorNum}`, press, ' hPa');
}

async function loadLog() {
  try {
    const response = await fetch('/event.log?t=' + new Date().getTime());
    const data = await response.text();
    // Получаем строки и переворачиваем массив
    allLogLines = data.split('\n').filter(line => line.trim() !== '').reverse();
    
    renderPage();
  } catch (error) {
    console.error('Ошибка загрузки:', error);
    const logContainer = document.getElementById('eventLog');
    if (logContainer) {
      logContainer.innerHTML = `<p style="color:red">Ошибка загрузки: ${error.message}</p>`;
    }
  }
}

function renderPage() {
  const logContainer = document.getElementById('eventLog');
  if (!logContainer || allLogLines.length === 0) return;

  const startIdx = currentPage * PAGE_SIZE;
  const pageLines = allLogLines.slice(startIdx, startIdx + PAGE_SIZE);

  logContainer.innerHTML = '';
  
  // Добавляем записи в перевёрнутом порядке (новые сверху)
  pageLines.forEach(line => {
    const p = document.createElement('p');
    if (line.includes('ERROR') || line.includes('Ошибка')) {
      p.style.color = '#d9534f';
    } else if (line.includes('WARNING') || line.includes('Предупреждение')) {
      p.style.color = '#f0ad4e';
    }
    p.textContent = line;
    logContainer.appendChild(p);
  });

  // Всегда скроллим вверх
  logContainer.scrollTop = 0;
  
  updatePagination();
}

function updatePagination() {
  const totalPages = Math.ceil(allLogLines.length / PAGE_SIZE);
  const pageInfo = document.getElementById('pageInfo');
  const prevBtn = document.getElementById('prevPageBtn');
  const nextBtn = document.getElementById('nextPageBtn');

  if (pageInfo) pageInfo.textContent = `Страница ${currentPage + 1} из ${totalPages}`;
  if (prevBtn) prevBtn.disabled = currentPage <= 0;
  if (nextBtn) nextBtn.disabled = currentPage >= totalPages - 1;
}

function nextPage() {
  currentPage++;
  renderPage();
}

function prevPage() {
  currentPage = Math.max(0, currentPage - 1);
  renderPage();
}

// Инициализация
window.addEventListener('load', () => {
  // Добавляем кнопки пагинации
  const logControls = document.querySelector('.log-controls');
  if (logControls) {
    logControls.innerHTML += `
      <button id="prevPageBtn" onclick="prevPage()" class="nav-btn" disabled>
        <i class="fas fa-arrow-left"></i> Назад
      </button>
      <span id="pageInfo" class="page-info">Страница 1 из ...</span>
      <button id="nextPageBtn" onclick="nextPage()" class="nav-btn">
        Вперед <i class="fas fa-arrow-right"></i>
      </button>
    `;
  }
  
  // Первая загрузка
  loadLog();
});

// Обновление даты и времени
function updateDateTime() {
  const now = new Date();
  const options = {
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false
  };
  
  const datetimeElement = document.getElementById('datetime');
  if (datetimeElement) {
    datetimeElement.textContent = now.toLocaleString('ru-RU', options);
  }
}
// функция обновления карточек напряжения
function updateVoltageCard(elementId, voltage, suffix = ' V') {
  const voltageValue = parseFloat(voltage) || 0;
  const element = document.getElementById(elementId);
  if (!element) return;

  const card = element.closest('.voltage-card');
  if (!card) return;

  // Сброс состояний
  card.classList.remove('normal', 'warning', 'network-on', 'network-off', 'generator-on', 'generator-off');
  card.style.animation = 'none';
  
  // Обновление значения напряжения
  if (voltageValue === null || voltageValue === undefined) {
    element.innerText = `--${suffix}`;
  } else {
    // ИЗМЕНЕНИЕ ЗДЕСЬ: для генератора показываем "остановлен" вместо "-- V"
    if (elementId === 'generatorVoltage') {
      if (voltageValue > 150) {
        const intVoltage = Math.round(voltageValue);
        element.innerText = `${intVoltage}${suffix}`; // Значение с "V"
        element.style.fontWeight = 'bold'; // Жирный для значений
      } else {
        element.innerText = 'Остановлен'; // Текст без "V"
        element.style.fontWeight = 'normal'; // Обычный шрифт
      }
    } else {
      const intVoltage = Math.round(voltageValue);
      element.innerText = `${intVoltage}${suffix}`;
    }
  }

  // Обработка карточки сети
  if (elementId === 'networkVoltage') {
    if (voltageValue > 150) {
      card.classList.add('network-on');
    } else {
      card.classList.add('network-off');
      element.innerText = `--${suffix}`; // Сеть: "-- V" если нет напряжения
    }
  }
  
  // Обработка карточки генератора
  if (elementId === 'generatorVoltage') {
    const smokeElements = card.querySelectorAll('.generator-smoke');
    const icon = card.querySelector('.voltage-icon');
    
    if (voltageValue > 150) {
      card.classList.add('generator-on');
      smokeElements.forEach(smoke => {
        smoke.style.display = 'inline-block';
        smoke.style.animation = 'smoke 3s infinite ease-out';
      });
      if (icon) {
        icon.style.color = 'var(--generator-icon-on)';
        icon.style.animation = 'icon-pulse 1.5s infinite, vibration 0.3s infinite alternate';
      }
    } else {
      card.classList.add('generator-off');
      smokeElements.forEach(smoke => smoke.style.display = 'none');
      if (icon) {
        icon.style.color = 'var(--generator-off-icon)';
        icon.style.animation = 'none';
      }
    }
  }
}

function updateStatusIndicator() {
  const networkState = document.getElementById('network-state').textContent;
  const generatorState = document.getElementById('generator-state').textContent;
  const statusIndicator = document.getElementById('status-indicator');
  
  // Сбрасываем анимацию и классы
  statusIndicator.style.animation = 'none';
  statusIndicator.classList.remove('network-off', 'generator-on', 'generator-off');
  
  // Логика отображения
  if (networkState === 'Отключено') {
    // Напряжение сети пропало - синий
    statusIndicator.classList.add('network-off');
    statusIndicator.style.backgroundColor = '#0000FF'; // Чистый синий
  } else if (generatorState === '1') {
    // Генератор работает (>150V) - зелёный с анимацией
    statusIndicator.classList.add('generator-on');
    statusIndicator.style.backgroundColor = '#00FF00'; // Ярко-зелёный
    statusIndicator.style.animation = 'vibration 0.3s infinite alternate';
  } else {
    // Генератор выключен (<150V) - синий
    statusIndicator.classList.add('generator-off');
    statusIndicator.style.backgroundColor = '#0000FF'; // Чистый синий
  }
}
// Публичные функции для управления
window.toggleCheckbox = function(element) {
  if (isConnected) {
    sendWebSocketCommand(element.id);
    updateElement(`${element.id}s`, element.checked ? 'ON' : 'OFF');
  }
};

window.toggleGenerator = function(element) {
  if (isConnected) {
    const command = element.checked ? 'start_generator' : 'stop_generator';
    sendWebSocketCommand(command);
    updateElement('generatorState', element.checked ? '🟢 Запущен' : '🔴 Остановлен');
  }
};

window.getReadings = function() {
  if (isConnected) {
    sendWebSocketCommand('getReadings');
  }
};

/*/ Инициализация графиков
function initCharts() {
  const sensors = ['temp1', 'hum1', 'press1', 'temp2', 'hum2', 'press2'];
  
  sensors.forEach(sensor => {
    const canvas = document.getElementById(`${sensor}-chart`);
    if (!canvas) return;

    const config = {
      unit: sensor.includes('temp') ? '°C' : 
            sensor.includes('hum') ? '%' : 'hPa',
      precision: 2
    };

    // Удаляем предыдущий график если существует
    if (sensorCharts[sensor]) {
      sensorCharts[sensor].destroy();
    }

    sensorCharts[sensor] = new Chart(canvas.getContext('2d'), {
      type: 'line',
      data: { 
        labels: [],
        datasets: [{
          label: ' ' + config.unit, // Пробел перед единицей измерения
          data: [],
          borderColor: '#1282A2',
          backgroundColor: 'rgba(18, 130, 162, 0.1)',
          borderWidth: 2,
          pointRadius: 3,
          tension: 0.3,
          fill: true
        }]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        scales: {
          x: {
            display: true,
            grid: { 
              display: true,
              color: 'rgba(0, 0, 0, 0.1)'
            },
            ticks: {
              autoSkip: true,
              maxRotation: 0,
              minRotation: 0
            }
          },
          y: {
            display: true,
            position: 'right', // Ось Y справа
            grid: { 
              display: true,
              color: 'rgba(0, 0, 0, 0.1)'
            },
            ticks: {
              callback: function(value) {
                // Целые числа в основных показаниях
                if (Number.isInteger(value)) {
                  return value + ' ' + config.unit;
                }
                // Сотые доли в графике
                return value.toFixed(config.precision) + ' ' + config.unit;
              }
            }
          }
        },
        plugins: {
          legend: { display: false },
          tooltip: {
            callbacks: {
              label: function(context) {
                return context.dataset.label + ': ' + context.parsed.y.toFixed(config.precision);
              }
            }
          }
        },
        interaction: {
          intersect: false,
          mode: 'nearest'
        }
      }
    });

    // Обработчик клика с правильным переключением
    let clickHandler = function() {
      if (currentDisplayMode === 'charts') {
        currentChartRange = currentChartRange === '24h' ? '1h' : '24h';
        updateChart(sensor);
        console.log(`Диапазон изменен на ${currentChartRange} для ${sensor}`);
      }
    };
    
    // Удаляем старый обработчик перед добавлением нового
    canvas.removeEventListener('click', clickHandler);
    canvas.addEventListener('click', clickHandler);
  });
}

function setupModeSwitcher() {
  document.getElementById('values-mode').addEventListener('click', () => {
      if (currentDisplayMode !== 'values') {
          currentDisplayMode = 'values';
          updateDisplayMode();
      }
  });

  document.getElementById('chart-mode').addEventListener('click', () => {
      if (currentDisplayMode !== 'charts') {
          currentDisplayMode = 'charts';
          currentChartRange = '24h';
          updateDisplayMode();
          loadChartData(); // Добавьте эту функцию для загрузки данных
      }
  });
}*/

function updateDisplayMode() {
  const valuesModeBtn = document.getElementById('values-mode');
  const chartModeBtn = document.getElementById('chart-mode');
  // Если элементы не найдены - выходим
  if (!valuesModeBtn || !chartModeBtn) {
    console.error('Элементы переключения режима не найдены!');
    return;
  }
  // Обновляем классы для визуального выделения
  valuesModeBtn.classList.toggle('active-mode', currentDisplayMode === 'values');
  chartModeBtn.classList.toggle('active-mode', currentDisplayMode === 'charts');
  
  // Переключаем видимость элементов
  document.querySelectorAll('.sensor-value').forEach(el => {
    el.style.display = currentDisplayMode === 'values' ? 'block' : 'none';
  });
  
  document.querySelectorAll('.sensor-chart').forEach(el => {
    el.style.display = currentDisplayMode === 'charts' ? 'block' : 'none';
  });
  
  // Если включен режим графиков - загружаем данные
  if (currentDisplayMode === 'charts') {
    loadChartData();
  }
}

/*function loadChartData() {
  const sensors = ['temp1', 'hum1', 'press1', 'temp2', 'hum2', 'press2'];
  sensors.forEach(sensor => {
      updateChart(sensor);
  });
}
// Обновление данных графика
function updateChart(sensorId) {
    // Запрос данных с сервера
    fetch(`/sensor-data?sensor=${sensorId}&range=${currentChartRange}`)
        .then(response => response.json())
        .then(data => {
            const chart = sensorCharts[sensorId];
            chart.data.labels = data.labels;
            chart.data.datasets[0].data = data.values;
            chart.update();
            
            // Клик по графику для переключения диапазона
            chart.canvas.onclick = () => {
                currentChartRange = currentChartRange === '24h' ? '1h' : '24h';
                updateChart(sensorId);
            };
        });
}*/