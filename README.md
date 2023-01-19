El objetivo de este proyecto ese crear un adaptador de corriente inteligente, el cual es capaz de enviar e interactuar con el usuario a traves de la plataforma thingdBoard y telegram a traves de un bot. Haciendo uso de comandos en Telegram se podra apagar y encender el adaptador y obtener el consumo que que generan los dispositivos conectadoe a el.

***IMPORTANTE***
Este proyecto consta de dos partes, medidor de corriente ac y snsor de movimiento. En la carpeta main se puede encontrar los archivos referentes al medidor de corriente ac. Para el sensor de movimiento se puede usar los mismo archivos que se encuentran en el directorio main a excepcion de app_main.c el cual debara ser sustitiodo por MovementSensor.c .
Recordar que cada parte debera encontrarse en placas de desarrollo distintas, por ello para esto proyecto se han empleado dos ESP32.
