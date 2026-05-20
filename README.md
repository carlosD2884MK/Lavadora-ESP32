# Lavadora ESP32

Esta lavadora puede usarse de dos maneras:

- Desde el panel local de la maquina
- Desde la pagina web del equipo

La idea es simple: eliges el tipo de ciclo, eliges la intensidad del lavado, eliges el tipo de agua y luego inicias. Tanto el panel como la pagina web controlan la misma lavadora, por lo que puedes usar el metodo que te resulte mas comodo.

## Que puede hacer

La lavadora permite:

- Ejecutar lavado completo
- Ejecutar enjuague y centrifugado
- Ejecutar solo lavado
- Ejecutar solo enjuague
- Ejecutar solo centrifugado
- Elegir intensidad de lavado
- Elegir agua fria, caliente o ambas
- Pausar, reanudar o cancelar un proceso

## Uso desde el panel local

En el panel frontal se observan tres botones principales:

- MODO
- CICLO
- START/STOP

Tambien hay grupos de luces indicadoras para mostrar:

- Ciclos y modos
- Agua fria o caliente
- Estado general de la lavadora

### Para que sirve cada boton

#### Boton MODO

Sirve para cambiar la intensidad del lavado. Cada vez que lo presionas, la lavadora avanza al siguiente modo disponible.

En terminos practicos:

- Un modo mas suave mueve menos la ropa y suele ser mejor para prendas delicadas
- Un modo mas fuerte trabaja con mas energia y durante mas tiempo

Encima de esta seleccion se usan los 3 LEDs superiores como referencia visual del modo elegido.

La lectura es asi:

- Si se enciende solo el LED mas a la derecha, el modo es Suave
- Si se enciende solo el LED del medio, el modo es Medio
- Si se encienden el LED del medio y el de la derecha, el modo es Fuerte
- Si se encienden los 3 LEDs, el modo es Muy Fuerte

Cuando cambias el modo, esas luces te permiten ver inmediatamente cual quedo seleccionado.

#### Boton CICLO

Sirve para cambiar el proceso que quieres ejecutar. Cada pulsacion avanza al siguiente ciclo.

Los ciclos disponibles son:

- Lavado + Enjuague + Centrifugado
- Enjuague + Centrifugado
- Solo Lavado
- Solo Enjuague
- Solo Centrifugado

Para el ciclo, los 3 LEDs superiores se interpretan asi:

- LED izquierdo: Lavado
- LED del medio: Enjuague
- LED derecho: Centrifugado

Segun el ciclo seleccionado, las luces quedan de esta manera:

- Lavado + Enjuague + Centrifugado: se encienden los 3 LEDs
- Enjuague + Centrifugado: se encienden el LED del medio y el derecho
- Solo Lavado: se enciende solo el LED izquierdo
- Solo Enjuague: se enciende solo el LED del medio
- Solo Centrifugado: se enciende solo el LED derecho

Asi puedes ver de un vistazo que partes del proceso estan incluidas en el ciclo elegido.

#### Boton START/STOP

Este boton controla la marcha del proceso.

Su comportamiento normal es:

- Si la lavadora esta lista, inicia el proceso
- Si la lavadora esta trabajando, la pausa
- Si la lavadora esta en pausa, la reanuda

Tambien se usa para detener o salir de un estado de error cuando sea necesario.

Si deseas cancelar o detener completamente el proceso, debes dejar presionado el boton START/STOP durante 2 segundos.

### Como elegir el tipo de agua

La lavadora permite trabajar con:

- Agua fria
- Agua caliente
- Ambas

Para cambiar el tipo de agua no se usa un boton separado. Debes presionar al mismo tiempo el boton CICLO y el boton MODO. Cada vez que haces esa combinacion, la lavadora cambia a la siguiente opcion de agua.

La seleccion se refleja con las luces del panel en la zona marcada como agua fria y caliente.

Si esta encendida:

- Solo la luz de fria, se usara agua fria
- Solo la luz de caliente, se usara agua caliente
- Ambas luces, se usaran ambas entradas de agua

### Significado de las luces del panel

#### Luces de ciclos o modos

Estas luces te ayudan a entender que has seleccionado o en que parte del proceso va la lavadora.

- En reposo muestran la seleccion hecha
- Durante el funcionamiento indican que fase esta activa
- Cuando una luz parpadea, normalmente significa que esa parte del proceso esta en ejecucion

#### Luces de agua

Indican el tipo de agua seleccionado.

- Fria
- Caliente
- Ambas

Durante el llenado pueden parpadear para indicar que la maquina esta entrando agua.

#### Led de estado

El led de estado sirve para avisar si la lavadora esta lista, trabajando, pausada o en error.

Segun el momento, puede quedar fijo o parpadeando.

### Ejemplo de uso desde el panel

#### Lavado completo

1. Presiona CICLO hasta dejar seleccionado el ciclo completo.
2. Presiona MODO hasta elegir la intensidad deseada.
3. Verifica el tipo de agua en las luces correspondientes.
4. Presiona START/STOP para iniciar.
5. Si necesitas detener temporalmente, vuelve a presionar START/STOP.
6. Para continuar, presiona otra vez START/STOP.

#### Solo centrifugado

1. Presiona CICLO hasta llegar a solo centrifugado.
2. Presiona START/STOP.
3. La maquina hara el proceso configurado para extraer el agua restante.

## Uso desde la pagina web

La lavadora tambien crea su propia red WiFi para controlarla desde un telefono o computadora.

### Como entrar

1. Conectate a la red WiFi llamada Lavadora-ESP32.
2. Ingresa la clave esp32wifi.
3. Abre el navegador.
4. Si no abre automaticamente, entra a http://4.3.2.1.

El nombre de la red WiFi y la clave pueden cambiarse facilmente al inicio del codigo editando los `#define` `WIFI_AP_SSID` y `WIFI_AP_PASSWORD` en [include/config.h](include/config.h).

### Que puedes hacer en la pagina

La pagina web muestra un panel de control sencillo donde puedes:

- Ver en que fase esta la lavadora
- Ver cuanto tiempo falta
- Ver si el nivel de agua esta lleno o vacio
- Elegir ciclo
- Elegir modo
- Elegir tipo de agua
- Iniciar
- Pausar o reanudar
- Cancelar
- Guardar la seleccion
- Ajustar tiempos de funcionamiento

### Seccion de estado

En la parte superior se muestra la informacion principal de la lavadora:

- Fase actual
- Tiempo restante total
- Tiempo restante de la etapa actual
- Estado general
- Nivel de agua
- Detalle del error si ocurre alguno

Esto permite saber rapidamente si la lavadora esta:

- Lista para iniciar
- Llenando
- Lavando
- Enjuagando
- Centrifugando
- Pausada
- En error

### Seccion de control

En esta parte eliges:

- El ciclo
- El modo
- El tipo de agua

Y luego puedes usar los botones para:

- Iniciar
- Pausar o reanudar
- Cancelar
- Guardar la seleccion para futuros usos

La opcion Guardar seleccion sirve para dejar almacenada la configuracion que la lavadora tomara por defecto cada vez que vuelva a energizarse.

Esto significa que, si guardas un ciclo, un modo y un tipo de agua, en futuras ocasiones la lavadora arrancara con esa misma configuracion ya preparada y solo hara falta presionar Iniciar para comenzar.

### Seccion de parametros

La pagina tambien permite ajustar la duracion de ciertas partes del proceso, por ejemplo:

- Tiempo total de lavado
- Tiempo total de enjuague
- Tiempo de centrifugado
- Tiempo de desague
- Tiempo maximo de llenado

Estos tiempos no son un ajuste general unico para toda la lavadora. Se configuran por separado para cada modo:

- Suave
- Medio
- Fuerte
- Muy Fuerte

Eso permite que cada modo tenga su propio comportamiento. Por ejemplo, puedes dejar un tiempo corto para Suave, uno intermedio para Medio y tiempos mas largos para Fuerte o Muy Fuerte.

En otras palabras, si cambias los tiempos de un modo, solo modificas ese modo y no los demas.

Esto sirve cuando se quiere adaptar el comportamiento de la maquina segun la cantidad de ropa, el tipo de prendas o la experiencia de uso.

Si no deseas modificar nada tecnico, puedes simplemente usar la pagina para iniciar y controlar la lavadora sin tocar esos valores.

## Comportamientos importantes

### Si la lavadora ya esta trabajando

Mientras un proceso esta en marcha, no se permite cambiar libremente la configuracion principal sin antes cancelar. Esto evita cambios accidentales a mitad de un ciclo.

### Si se pausa

Cuando pausas la lavadora:

- Se detiene el proceso actual
- La maquina espera
- Luego puede continuar desde donde iba

### Si termina

Cuando el ciclo finaliza, la lavadora avisa con luces y sonido para indicar que el proceso ha terminado.

### Si ocurre un error

Si hay un problema, por ejemplo que no se alcance el nivel de agua esperado, la lavadora entra en estado de error para proteger el proceso. En ese caso:

- Las luces lo indicaran
- La pagina web mostrara el motivo
- Debe revisarse la causa antes de volver a iniciar

## Recomendacion de uso

Para una operacion normal del dia a dia:

1. Selecciona el ciclo.
2. Selecciona el modo.
3. Verifica el tipo de agua.
4. Inicia.
5. Si quieres seguir el avance con mas detalle, abre la pagina web.

## Mas informacion

Si necesitas una explicacion mas amplia del funcionamiento interno y de todas las opciones disponibles, revisa [MANUAL.md](MANUAL.md).