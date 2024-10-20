var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
window.addEventListener('load', onLoad);

function onLoad(event) {
    initWebSocket();
}

function getReadings(){
    websocket.send("getReadings");
}

function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen    = onOpen;
    websocket.onclose   = onClose;
    websocket.onmessage = onMessage;
}

function onOpen(event) {
    console.log('Connection opened');
    websocket.send("states");
    //getOutputStates();
}
  
function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
} 

function onMessage(event) {
    var myObj = JSON.parse(event.data);

//    var keys = Object.keys(myObj);
//    for (var i = 0; i < keys.length; i++){
//      var key = keys[i];
//      document.getElementById(key).innerHTML = myObj[key];
//    }
            console.log(myObj);
            for (i in myObj.gpios){
                var output = myObj.gpios[i].output;
                var state = myObj.gpios[i].state;
                console.log(output);
                console.log(state);
                if (state == "1"){
                    document.getElementById(output).checked = true;
                    document.getElementById(output+"s").innerHTML = "ON";
                }
                else{
                    document.getElementById(output).checked = false;
                    document.getElementById(output+"s").innerHTML = "OFF";
                }
            }
    console.log(event.data);
}

// Send Requests to Control GPIOs
function toggleCheckbox (element) {
    console.log(element.id);
    websocket.send(element.id);
    if (element.checked){
        document.getElementById(element.id+"s").innerHTML = "ON";
    }
    else {
        document.getElementById(element.id+"s").innerHTML = "OFF"; 
    }
}

function toggleGenerator(element) {
    const isChecked = element.checked;
    if (isChecked) {
        // Вызов функции для запуска генератора
        fetch('/start_generator', { method: 'POST' });
        document.getElementById('generatorState').innerText = "Запущен";
    } else {
        // Вызов функции для остановки генератора
        fetch('/stop_generator', { method: 'POST' });
        document.getElementById('generatorState').innerText = "Остановлен";
    }
}