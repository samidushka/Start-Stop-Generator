# Start-Stop-Generaor

Добро пожаловать в проект контроллера ESP32 для автостарта генератора ELP LH45iE.

Логика автоматической работы: Генератор запускается(4 попытки) спустя 3 мин. после отключения напряжения с города и остнавливается спустя 1 мин. после влючения напряжения города.

Описание состояния RGB индикатора:
Белый мигает - есть напряжение с города и подключен WiFi
Розовый мигает - есть напряжение с города, но не подключен WiFi
Синий горит - автостарт генератора
Жёлто-красный мигает - пропало напряжение с города
Зеленый-красный мигает - если бензо-клапан закрыт, но генератор работает

Для управления контроллером используйте следующий команды:
/gen_start : запустить генератор
/gen_stop : заглушить генератор
/status : получить сотояние генератора, WiFi, получить температуру/влажность/давление с датчиков у контроллера и генратора

В zip/v6_RELISE_bin.zip три бинарника bootloader.bin, partitions.bin, firmware.bin - имя сети WIFI: test пароль: test1234 доступ по http://ip/

В плнах отправлять в zabbix и перейти на FastBot2 для включения ESP.restart()(которы сейчас уходит в bootloop)