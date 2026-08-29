# Nodo — suite de plugins

**by SonidoenRed**

Monorepo de la suite de plugins gratuitos. Siete hasta ahora:

- **Nodo EQ** — ecualizador paramétrico de 24 bandas con analizador en tiempo
  real, proceso M/S, modo dinámico por banda y corrección de cramping.
- **Nodo Comp** — compresor con nueve estilos, compresión descendente y
  ascendente, sidechain externo con filtros, lookahead, hold, range y los tres
  automatismos (umbral, release y ganancia).
- **Nodo Limit** — limitador true peak con lookahead, medición de sonoridad
  BS.1770 (momentánea, corto plazo e integrada), dither y modo delta.
- **Nodo Ess** — de-esser con crossover Linkwitz-Riley, detección adaptativa,
  techo de banda del detector, proceso de solo mid o solo side, y modo de banda
  dividida o banda ancha.
- **Nodo Gate** — puerta y expansor con histéresis, hold, lookahead, seis
  algoritmos, sidechain externo con filtros, ducking, proceso enlazado / L-R /
  M-S y disparo por MIDI.
- **Nodo Delay** — delay estéreo de dos líneas con sincronía al tempo,
  ping-pong, realimentación cruzada, filtros, saturación y lo-fi dentro del
  lazo, comportamiento de cinta con cambio de tono, wow y flutter, freeze,
  patrón rítmico de hasta 16 pasos y matriz de modulación con dos LFOs y un
  seguidor de envolvente.
- **Nodo Verb** — reverb de red de retardos realimentada con **tiempo de caída
  distinto por frecuencia**, dibujado y arrastrable en segundos, más tamaño
  independiente del decaimiento, predelay, difusión, movimiento, filtros de
  entrada, EQ posterior de tres bandas, ducking y freeze exacto.

- **Nombre de producto:** `Nodo EQ` (lo que se ve en la lista de inserts)
- **Fabricante:** `SonidoenRed` (por eso Pro Tools los agrupará juntos)
- **Formatos:** VST3 y Standalone en todas las plataformas, AU además en macOS

---

## Puesta en marcha

```bash
./setup.sh                                  # clona JUCE 9.0.1 en libs/JUCE
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

En Windows, con Visual Studio 2022:

```bat
git clone --depth 1 --branch 9.0.1 https://github.com/juce-framework/JUCE.git libs\JUCE
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

`COPY_PLUGIN_AFTER_BUILD` está activado, así que al compilar el plugin se copia
solo a la carpeta de plugins del sistema y aparece en el DAW tras un rescan.

Los binarios quedan en:

```
build/plugins/nodo_eq/nodo_eq_artefacts/Release/
├── VST3/Nodo EQ.vst3
├── AU/Nodo EQ.component          (solo macOS)
└── Standalone/Nodo EQ
```

### Empaquetado para la web

```bash
./packaging/empaquetar.sh            # todos los plugins compilados
./packaging/empaquetar.sh nodo_eq    # solo uno
```

Deja en `dist/` un ZIP por plugin y por sistema —`Nodo-EQ-1.0.0-macOS.zip`— con
el VST3, el Audio Unit si el sistema lo tiene, la aplicación suelta y las
instrucciones de instalación en español y en inglés, sacadas de
`packaging/plantillas/` con el nombre del plugin sustituido.

Un ZIP por plugin y no uno con la suite: cada plugin tiene su ficha y su botón
de descarga en la web, y quien entra a por el ecualizador no tiene por qué
bajarse siete. El CI llama a este mismo script, así que el artefacto de GitHub
Actions es exactamente el archivo que se sube a sonidoenred.com.

Dentro no hay instalador ni nada ejecutable aparte del propio plugin: eso es lo
que mantiene a SmartScreen y a los antivirus fuera de la conversación. En macOS
sí hay que quitar la cuarentena a mano, y las instrucciones lo explican, porque
firmar cuesta 99 dólares al año y la suite es gratis.

### Verificación de DSP

```bash
cmake --build build --target nodo_tests
./build/tests/nodo_tests_artefacts/Release/nodo_tests
```

382 comprobaciones. Del EQ, 135, sobre la respuesta de los filtros: ganancia exacta en el centro
de las campanas, −3,01 dB en la frecuencia de corte, pendientes asintóticas de
12 a 96 dB por octava, planitud de la banda pasante, planitud absoluta del all
pass, el recorrido completo del tilt shelf, los filtros de escucha del solo (un
low cut soleado tiene que dejar pasar lo que quita, no lo que conserva), barrido
aleatorio de 4000 diseños buscando NaN o inestabilidad, calibración del
analizador, precisión de la medida de pico, un barrido completo del diseño
analógico contra su prototipo, y el enrutado M/S y L/R medido de extremo a
extremo: una banda Mid tiene que ser sorda a una señal puramente lateral, y la
ida y vuelta a mid/side tiene que devolver la señal intacta hasta la millonésima
de fondo de escala. Y la sección dinámica entera: la forma cerrada de la rodilla,
la monotonía de la curva de ganancia, los tiempos del seguidor de envolvente
medidos contra su constante de tiempo, y el comportamiento de extremo a extremo
con material fuerte, flojo y fuera de banda. Y los presets de fábrica: que cada
identificador que escriben existe de verdad, que cada valor cabe en su rango, que
ninguno deja encendida una banda del preset anterior, y que el de-esser baja la
sibilancia sin tocar una vocal al mismo nivel.

Del compresor, 54: la curva estática contra su forma cerrada (7,5 dB de
reducción a 4:1 diez dB por encima del umbral, la rodilla valiendo exactamente
pendiente·rodilla/8 en el umbral, la rodilla empalmando con las dos rectas sin
escalón), el estado estacionario medido a través del motor, los tiempos de
ataque y release contra el 63 % de una exponencial, el RMS de una senoide a
−3,01 dB, el link estéreo en sus dos extremos, el filtro de sidechain frenando
un tono de 40 Hz, el ducking desde una entrada externa, la latencia del
lookahead comprobada con un impulso, el auto gain devolviendo el nivel, y que
ningún estilo toque una señal 25 dB por debajo del umbral. Y lo añadido después:
el range topando la reducción, la curva ascendente como imagen especular de la
descendente, el ataque sin intercambiarse al cambiar de dirección, el hold
sujetando la ganancia 30 ms después de que pare la señal mientras que sin hold
ya la ha soltado, el umbral automático dando el mismo trabajo a dos señales
separadas 20 dB, y el auto release tardando más en soltar tras una reducción
profunda que tras una superficial.

Del limitador, 35, y del de-esser, 26. Del de-esser conviene destacar dos: que
el techo del detector le quita al 16 kHz exactamente los decibelios que su
propio filtro le quita —la expectativa se calcula con la magnitud del filtro, no
con la cifra de manual— y que en modo mid o side la rotación de ida y vuelta
devuelve la señal **muestra a muestra**, no solo en energía, comparada contra el
modo estéreo. Esa segunda comprobación es la que atrapa el error clásico:
filtrar solo la componente que se trata deja la otra llegando con otra fase, y
la suma peina.

Del gate, 42. Dos cosas que merece la pena destacar de cómo están hechos. La
mayoría se miden **a través del sidechain externo**, porque es la única forma
honesta de ver la envolvente de una puerta: lo que se está midiendo es la
ganancia, y una vez que la puerta se ha cerrado no queda señal en la salida con
la que medirla. Un tono constante en la entrada y la señal que decide en el
sidechain convierten la salida en una lectura directa de la ganancia, en dB,
muestra a muestra. Y el tiempo de release se mide **como una razón entre dos
puntos de la caída**, no desde que cae el sidechain: el detector tiene
balística propia —15 ms de release— y tarda unos 40 ms en cruzar el umbral
después de que pare la señal. Medir desde la caída habría medido la suma de dos
constantes de tiempo distintas y habría llamado a eso el release.

Del delay, 65, y todas ellas cuelgan de una idea: el dibujo de dónde cae cada
repetición lo calcula `buildTapPattern()`, y el audio lo produce un código
completamente distinto. Si un impulso a través del motor no cae exactamente
donde el dibujo dice, al nivel exacto que el dibujo dice, uno de los dos miente
y da bastante igual cuál. **Coinciden hasta el error de coma flotante.** Lo
demás: la interpolación del delay fraccionario contra el seno analítico, la
sincronía a tempo, el ping-pong como enrutado, la cinta doblando el tono
mientras el cabezal viaja y el modo fade no haciéndolo, el freeze sin decaer y
sin dejar entrar nada, el 110 % de realimentación acabando en un tono y no en
infinito, el filtro del lazo mordiendo lo mismo en cada generación, y el
antialiasing del saturador medido como el tercer armónico plegado.

De la fase dos del delay, la comprobación que más importa es la más aburrida de
leer: **con el multi-tap apagado y las ranuras de modulación a cero, ochenta
parámetros nuevos no mueven ni una muestra**. El test rellena los dieciséis
pasos, configura los dos LFOs con formas y velocidades distintas, apunta tres
ranuras a tres destinos, deja las cantidades en cero, y exige que la salida sea
idéntica bit a bit a la de antes de que nada de eso existiera. Una sesión
guardada en la fase uno tiene que abrirse y sonar igual, y eso no se puede
comprobar de oído.

De la reverb, 25, y todas giran alrededor de una sola cantidad: **el tiempo de
caída es medible**. La pantalla dibuja segundos contra frecuencia; el test pasa
una ráfaga de ruido por el motor, la filtra en banda estrecha, ajusta una recta
por mínimos cuadrados entre −5 y −35 dB de la caída y la extrapola a −60. Si el
número medido y el número dibujado coinciden, lo que se ve y lo que se oye son
lo mismo. Coinciden: pedidos 0,80 · 2,00 · 5,00 s, medidos 0,80 · 2,04 · 5,03.
Y con la curva inclinada —el grave el doble de largo, el agudo la mitad— los
tres extremos se miden por separado dentro de la misma cola: 3,90 s dibujados y
4,10 medidos a 80 Hz, 2,00 y 2,01 a 1 kHz, 1,03 y 1,11 a 9 kHz. El resto: un
barrido de cuarenta ajustes aleatorios comprobando que ninguno crece en vez de
caer, el predelay sin dejar salir nada antes de tiempo, el freeze moviéndose
0,01 dB en trece segundos —exactamente para siempre, no casi— y el ducking
hundiendo la cola los 20,0 dB que promete su mando.

Lo importante es que compara la respuesta analítica que se dibuja en pantalla
contra la medida real de una señal pasada por el motor: si coinciden, lo que el
usuario ve y lo que oye son lo mismo. En el compresor esa comparación se hace
con onda cuadrada, cuya envolvente rectificada es constante; sobre una senoide
la ganancia riza dentro de cada ciclo —el detector ve el pico, el nivel se
desploma hacia cero y el release empieza a soltar antes del pico siguiente— y
esa diferencia, medida en 0,29 dB, también está fijada por un test para que no
crezca sin que nadie se entere.

Y una comprobación que no mide DSP y que está en los siete: **que los acentos de
los presets llegan a la pantalla**. `juce::String` interpreta un `const char*`
como ASCII, así que "Sala pequeña" salía en el menú como "Sala pequeÃ±a" — texto
válido, ningún error, y nadie se entera hasta que abre el menú y lo mira. Los
presets de fábrica de esta suite están escritos en español, o sea que eso era
todos. Ahora pasan por un `utf8()` que hace la conversión bien, y el test busca
los dos caracteres que delatan el desastre.

### Validación con pluginval

El CI pasa **pluginval en nivel de exigencia 10**, el máximo, en las tres
plataformas. Cubre justo lo que las pruebas de DSP no pueden: abrir y cerrar el
editor cientos de veces, automatizar los más de 300 parámetros desde el host,
guardar y restaurar el estado desde un hilo que no es el de mensajes, tamaños de
bloque raros, configuraciones de buses y un pase de fuzzing. Es lo más parecido
que hay a "¿funcionará en el DAW de otra persona?" sin tener ese DAW delante.

Para ejecutarlo a mano:

```bash
pluginval --strictness-level 10 --validate-in-process \
          --validate "build/plugins/nodo_eq/nodo_eq_artefacts/Release/VST3/Nodo EQ.vst3"
```

**Un aviso sobre pluginval y los parámetros de tipo interruptor.** Su prueba de
restauración de estado lee el valor de un parámetro, le mete uno aleatorio,
restaura el estado y comprueba que ha vuelto. Con un interruptor eso no cuadra:
el envoltorio VST3 guarda el 0,139 que le han metido, el plugin lo entiende como
"apagado" y devuelve 0, y la diferencia supera la tolerancia de 0,1. Aparece al
azar según los valores que le toquen, y le pasa a cualquier plugin con
interruptores — el ecualizador tiene 48, así que es al que más le sale. **No se
ejecuta con `--randomise`**, que es la forma más rápida de encontrárselo. Si
alguna vez el CI se pone rojo con "not restored on setStateInformation", es
esto.

### Consumo de CPU

```bash
cmake --build build --target nodo_bench
./build/tests/nodo_bench_artefacts/Release/nodo_bench
```

Mide qué parte de un núcleo necesita el motor para ir a tiempo real, que es la
pregunta que de verdad se hace quien mezcla: cuántas instancias caben. Los
números absolutos dependen de la máquina; lo que importa son las proporciones
entre filas.

Una referencia medida en el contenedor de desarrollo, a 48 kHz y bloques de 256:
ocho bandas estáticas del EQ cuestan un 0,32 % de un núcleo (unas 314
instancias), y 24 bandas todas dinámicas un 3,60 % (unas 28). El caso peor
imaginable —24 bandas dinámicas con todas las frecuencias barriendo a la vez— se
queda en un 6,52 %. El compresor no llega al 0,3 % en ninguna configuración. La
reverb es el plugin caro de la suite: un 2,0 % pase lo que pase con los mandos,
unas 50 instancias por núcleo.

Cuatro cosas que enseña la tabla y conviene tener presentes:

- **Mover parámetros cuesta unas seis veces más que estar quieto**, porque cada
  trozo de 32 muestras recalcula coeficientes. Es la contrapartida elegida a
  cambio de que los cambios no produzcan clics, y solo se paga mientras arrastras
  o hay automatización.
- **El modo Analog multiplica por 3,6 el coste de barrer** frente al modo
  Digital, por la verificación del ajuste contra el prototipo. Se deja así a
  propósito: correcto antes que rápido, y barrer 24 bandas simultáneamente no le
  pasa a nadie. Si alguna vez molesta, ahí está el sitio donde mirar.
- **En la reverb los mandos no cambian el coste.** Una cola de dos segundos y
  una de diez valen lo mismo: las ocho líneas y sus dieciséis shelves corren
  igual, y lo único que cambia es lo que dicen los coeficientes. El EQ posterior
  y el ducking suman un 10 % entre los dos, y el freeze *ahorra* —se salta los
  dieciséis filtros— y baja a un 1,58 %.
- **La medición se hace con flush-to-zero**, que es el estado en el que el host
  llama a `processBlock`. Sin él, la reverb congelada medía tres veces su coste
  real: no entra nada nuevo, lo que queda dentro cae hacia cero, y la aritmética
  denormal que eso produce es lo más lento que hay en un procesador. Habría sido
  medir un estado en el que el plugin no funciona nunca.

---

## Estructura

```
shared/nodo_core/     DSP, parámetros, presets y versionado de estado
shared/nodo_ui/       look & feel, widgets y barra superior comunes
plugins/nodo_eq/      el ecualizador
plugins/nodo_comp/    el compresor
plugins/nodo_limit/   el limitador
plugins/nodo_ess/     el de-esser
plugins/nodo_gate/    la puerta y expansor
plugins/nodo_delay/   el delay
plugins/nodo_verb/    la reverb
tests/                verificación de DSP y medición de rendimiento
packaging/            instrucciones de instalación que van dentro del ZIP
cmake/NodoPlugin.cmake  política común de todos los plugins de la suite
```

Los dos módulos compartidos son lo que hace que la suite parezca una suite. El
compresor no ha tenido que escribir barra superior, sistema de presets, tema
visual, medidores ni versionado de estado: aportó su DSP y su pantalla. La
reverb, que es el plugin más grande de los siete por dentro, entró igual: lo
único suyo son el motor, la curva de caída y la pantalla que la dibuja.

### Decisiones que conviene no deshacer sin pensarlo

- **`shared/nodo_core/dsp/Biquad.h` en vez de `juce::dsp::IIR`.** Las funciones
  de JUCE reservan memoria cada vez que se calculan coeficientes. El EQ los
  recalcula cada 32 muestras para que los cambios de parámetro no produzcan
  clics, y reservar memoria miles de veces por segundo en el hilo de audio es
  justo lo que provoca cortes bajo carga.
- **Suavizado en chunks de 32 muestras** (`EqEngine`). Actualizar coeficientes
  una vez por bloque se oye como escalones; por muestra es un desperdicio.
- **La frecuencia se suaviza en espacio log2**, para que un barrido avance a
  octavas por segundo constantes.
- **Estado versionado desde el día uno** (`StateVersion.h`). La primera
  actualización que renombre un parámetro no puede romper las sesiones de nadie.
- **Los IDs de parámetro son para siempre.** Están escritos en cada sesión
  guardada. Renombrar uno exige un paso de migración.
- **El orden del enum `FilterType` está congelado.** El índice se guarda en cada
  sesión: meter una forma nueva en medio convertiría cada campana guardada en un
  notch. Las nuevas van al final.
- **`Dynamics.h` vive en `nodo_core`, no en el EQ.** El seguidor de envolvente y
  el cálculo de ganancia son los mismos que necesitará el compresor. Es la
  diferencia entre escribir un compresor más adelante y volver a escribir su
  seguidor de envolvente más adelante.
- **El orden de las bandas es parte del sonido** en cuanto hay bandas M/S. No
  reordenar el bucle de proceso para optimizar.
- **Los cortes también son analógicos.** Un paso bajo bilineal tiene un cero
  doble en Nyquist, así que ahí vale menos infinito mientras el prototipo que
  dice ser todavía tiene un valor real. Medido: un high cut de 16 kHz a
  44,1 kHz da 0,62 dB de error máximo con el diseño ajustado frente a 14,48 dB
  con el bilineal, y a 21,6 kHz el prototipo pide −6,36 dB, el ajustado da
  −6,60 y el bilineal −46,62. Cada sección de la cascada se ajusta por separado
  con su propia Q de Butterworth.
- **`MatchedBiquad` nunca empeora un ajuste.** Construye dos candidatos, los mide
  contra el prototipo analógico junto al diseño bilineal y se queda con el más
  cercano. Si ninguno gana, usa el bilineal.

---

## Nodo EQ: qué hace

**24 bandas**, cada una con nueve formas: low cut, low shelf, bell, notch, band
pass, tilt shelf, all pass, high shelf y high cut. Frecuencia 20 Hz – 20 kHz,
ganancia ±24 dB, Q 0,1 – 18, y pendientes de 12 a 96 dB/oct en los cortes.

**Interacción sobre la curva**, con las reglas que ya conoce todo el mundo:

| Gesto | Efecto |
|---|---|
| Doble clic en zona vacía | crea una banda ahí |
| Doble clic en una tecla del teclado | crea la banda en esa nota exacta |
| Arrastrar un nodo | frecuencia y ganancia |
| Rueda del ratón sobre un nodo | Q, o la pendiente si es un corte |
| Alt + clic sobre un nodo | solo de esa banda |
| Doble clic sobre un nodo | desactiva la banda |
| Clic derecho sobre un nodo | tipo, pendiente, solo, invertir ganancia, borrar |

**Solo por banda.** Aísla la zona sobre la que actúa la banda: band pass para
campanas y notches, paso bajo o alto para shelves, y para los cortes el
complemento del filtro, de modo que oyes justo lo que estás tirando. Es la
función más didáctica del plugin y por eso está en el primer lote.

**Lo que se quitó: el enganche al pico.** Al pasar el ratón cerca de un pico del
analizador aparecía un marcador con su frecuencia y su nota, y al pulsarlo se
creaba la banda ahí. La idea era buena y el resultado no: el pico salía del
espectro vivo, así que el marcador **bailaba con la música en vez de seguir al
ratón** — se movía solo con el ratón quieto. Para caer sobre una nota exacta está
el teclado, que no se mueve. La búsqueda de picos sigue en `nodo_core` con sus
tests: refina por interpolación parabólica entre bins y cae a menos de 2 Hz del
pico real, y el día que haya un "pégate al pico" con una tecla pulsada será eso.

**Teclado y piano roll.** El botón KEYS saca un teclado a lo largo de la parte
de arriba del gráfico y sombrea los semitonos por debajo, para leer el espectro
en términos musicales. El teclado no es solo un dibujo: **doble clic en una
tecla crea una banda en esa nota exacta**, cada banda deja un punto de su color
sobre la tecla en la que cae, y un clic en una tecla con banda la selecciona.

Las teclas blancas se tocan entre ellas y las negras van encima, como en un
piano, pero repartidas sobre el eje logarítmico de frecuencia y no a anchuras
iguales: así cada tecla está justo encima de su frecuencia, que es para lo que
sirve tenerlo ahí. Va arriba porque abajo está el eje de frecuencias con sus
cifras, y dos reglas pegadas compiten.

**EQ dinámico por banda.** Cualquier banda con ganancia puede volverse dinámica.
El modelo es deliberadamente simple de explicar: **la ganancia dice dónde vive la
banda y el recorrido dice cuánto se mueve desde ahí**. Con el recorrido a cero,
encender la dinámica no cambia una sola muestra — que es la única forma de que
encenderla no sea una sorpresa. El recorrido se ajusta arrastrando un segundo
tirador sobre la curva, y el área entre los dos es todo lo que la banda puede
llegar a hacer; la curva blanca se mueve por dentro. Modo *Above* para
domar una resonancia, una sibilante o una nota que se dispara; modo *Below* para
levantar detalle que solo aparece en lo flojo. Umbral, ratio, ataque y relajación
por banda, con rodilla suave fija de 6 dB — una rodilla dura castañetea de forma
audible con material que se sienta justo en el umbral, que es exactamente donde
la gente lo pone.

Dos decisiones que importan más de lo que parece:

- **El detector usa el mismo filtro que el botón SOLO.** Lo que dispara la
  dinámica es exactamente lo que oyes al soltear la banda. Sin eso acabas
  explicando por qué un de-esser se cierra con algo que no encuentras.
- **El detector lee la entrada del EQ, no la señal en curso.** Si leyera la señal
  ya procesada por las bandas anteriores, dos bandas dinámicas en la misma zona
  se perseguirían la cola. Cada trozo de 32 muestras se copia antes de que
  ninguna banda lo toque.

El detector respeta la colocación estéreo de la banda: una banda Mid dinámica se
dispara con material centrado y una Side con los laterales.

En la pantalla, la banda dinámica se dibuja **donde está ahora mismo**, no donde
apunta su mando, así que la curva respira con la música. Una marca tenue señala
el extremo de su recorrido. El nodo lleva la etiqueta DYN.

*Limitación honesta:* la ganancia dinámica se actualiza cada 32 muestras
(0,67 ms a 48 kHz), que es el ritmo al que se recalculan los coeficientes. La
envolvente se sigue muestra a muestra, pero ataques por debajo de 1 ms quedan
cuantizados por ese ritmo de control. Para domar resonancias y sibilantes sobra;
para un limitador no serviría.

**M/S y L/R por banda.** Cada banda actúa sobre Stereo, Left, Right, Mid o Side.
Las bandas se aplican **en orden estricto**, rotando a mid/side solo cuando la
siguiente banda lo necesita: un filtro sobre Left y otro sobre Mid son ambos
diagonales pero en bases distintas, y operadores diagonales en bases distintas no
conmutan. Reordenar las bandas para ahorrarse una rotación cambiaría el sonido en
silencio. En un bus mono todas las bandas se comportan como Stereo.

Cuando alguna banda deja de ser Stereo, la curva se parte en dos: trazo continuo
para el primer componente de salida (L o M) y discontinuo para el segundo (R o
S), con la leyenda arriba a la izquierda. Los nodos afectados llevan su letra.
Si mezclas bandas L/R con bandas M/S en la misma instancia, la leyenda pasa a
"L / M" y "R / S": son dos lecturas honestas de lo que le pasa a cada componente
de salida, no una función de transferencia única, que en ese caso no existe.

**Modo de filtro: Digital o Analog.** Digital es el diseño bilineal (recetario
RBJ) que usa casi todo el mundo. Analog vuelve a derivar campanas y shelves para
que sigan al prototipo analógico hasta Nyquist, en vez de quedar aplastados
contra él. Una campana de +12 dB a 10 kHz a 44,1 kHz se queda a 3,8 dB de donde
debería en modo Digital; en Analog, a 0,7 dB. Es el equivalente de lo que otros
plugins venden como Natural Phase, y hace que el oversampling deje de hacer
falta casi siempre. Cortes, notches, band pass y all pass son bilineales en
ambos modos.

**Analizador FFT** de 4096 puntos con ventana Hann, ataque instantáneo y caída
lenta, eje logarítmico e inclinación de 4,5 dB/oct referida a 1 kHz para que el
material musical se lea aproximadamente plano. Modos pre, post, ambos o apagado.

**Además:** oversampling 2× opcional con latencia declarada al host, bypass con
crossfade (sin clic), ganancia de salida, auto-gain, invertir ganancia,
comparación A/B, presets en `~/Documents/Nodo/Nodo EQ/Presets`, ventana
redimensionable y escala de visualización de ±6 / ±12 / ±30 dB.

### Presets de fábrica

21 presets en cinco categorías, y están deliberadamente divididos en dos clases
distintas, porque los presets de EQ tienen mala fama merecida: un realce a 3 kHz
"para voz" depende por completo de esa voz, ese micro y esa mezcla.

- **Utility, Dynamic, Tone y Stereo son objetivos.** Un corte de graves a 80 Hz,
  cuatro notches sobre el zumbido de red, un de-esser, graves en mono por debajo
  de 150 Hz: son respuestas a problemas que existen al margen del gusto, y son
  los que de verdad merecen un atajo.
- **Starting point es andamio, y lo dice en su propia descripción.** Para quien
  está aprendiendo, una disposición sensata de bandas que luego mover vale más
  que una ventana en blanco. Para cualquier otro es un clic desperdiciado, y eso
  también está bien.

Viven en `plugins/nodo_eq/source/FactoryPresets.cpp` descritos en términos
humanos —`bell (300.0f, -2.0f, 1.0f)`— y no como listas de identificadores, así
que añadir uno son dos líneas. La descripción de cada preset aparece como
tooltip sobre el nombre, que es donde de verdad se enseña algo.

El soporte de presets de fábrica está en `nodo_core`, no en el EQ: el compresor
tendrá los suyos con el mismo mecanismo.

### Por qué el solo no es un parámetro

Es una decisión deliberada: soltear es una acción de escucha, no parte del
sonido de un preset. Si fuese automatizable, una sesión guardada podría abrirse
con una banda en solo y el usuario se pasaría media hora buscando por qué la
mezcla suena a teléfono.

### Distribución

El CI empaqueta un **ZIP, no un instalador**: dentro solo va la carpeta del
plugin y un archivo de instrucciones. Nada ejecutable, así que SmartScreen no
entra en juego en Windows y no hace falta firmar nada para publicar la versión
de Windows. Los ZIP salen como artefactos de cada build, incluidos los de
Windows, de modo que puedes publicar para Windows sin tener un Windows.

En macOS el usuario sí tiene que quitar la cuarentena a mano mientras el plugin
no esté firmado y notarizado; las instrucciones lo explican sin rodeos y sin
pedir perdón de más.

### Pendiente, en orden

1. **Licencia.** Falta decidir bajo qué licencia se publican y añadir el
   archivo correspondiente al repositorio.
2. **Multi-tap y modulación en la reverb**, si alguna vez hacen falta: hoy no
   están y no se echan de menos.
3. **AAX y Pro Tools**, fase dos del proyecto.

---

## Nodo Comp: qué hace

Un compresor limpio con **cinco estilos**. Un estilo no es otro compresor: es un
juego de multiplicadores sobre lo que has pedido tú, más dos decisiones
estructurales. Todo lo que hace cada uno está en `styleTraits()`, a propósito —
un modo "vintage" que no se puede explicar es un modo que no se puede depurar.

| Estilo | Rodilla | Ataque | Release | Qué lo distingue |
|---|---|---|---|---|
| Clean | ×1 | ×1 | ×1 | hace exactamente lo que dicen los mandos |
| Punch | ×0,5 | ×0,6 | ×0,8 | deja pasar el transitorio antes de mover la ganancia |
| Opto | ×2,5 | ×1,6 | ×2 | detección por realimentación, muy dependiente del programa |
| Vocal | ×1,8 | ×1 | ×1,2 | rodilla ancha, release que sigue a la frase |
| Bus | ×2,2 | ×1,3 | ×1,5 | dos etapas de release: pegamento, no control |
| Classic | ×1,2 | ×1 | ×1 | realimentado y comedido: el de propósito general |
| Master | ×3 | ×1,6 | ×1,8 | todo ancho y lento; dos dB y ni se nota |
| Smooth | ×2 | ×1,4 | ×1,6 | dependencia del programa al 75 %: sigue el nivel |
| Pump | ×0,3 | ×0,5 | ×0,3 | release cortísimo contra rodilla dura: el efecto, queriendo |

**Detección por realimentación** en Opto y Bus: el detector lee la salida del
elemento de ganancia en vez de la entrada, que es donde está en el hardware al
que se parecen. Un compresor realimentado nunca aprieta tanto como sugieren los
números, porque solo puede reaccionar a lo que ya ha dejado pasar.

**Release dependiente del programa** y **doble etapa**, ambos medibles: el
release se alarga con la profundidad de la reducción (al doble con 6 dB de
reducción y dependencia total), y la segunda etapa corre en paralelo cuatro
veces más lenta, ganando la más profunda de las dos.

**Compresión ascendente.** El mismo motor apuntando al revés: levanta lo que
cae por debajo del umbral en vez de bajar lo que sobresale. Internamente la
ganancia ascendente se niega antes de entrar al suavizador y otra vez al salir,
para que "más proceso" siga significando lo mismo para el ataque y el release —
si no, los dos mandos se intercambian al cambiar de dirección. Va siempre
acompañada de **Range**, porque un gain computer sin tope amplifica el ruido de
fondo cuarenta decibelios encantado de la vida.

**Range** limita cuánto puede moverse la ganancia, y **Hold** mantiene la
reducción un rato antes de soltar: sin hold, una señal que baja unos
milisegundos entre palabras o entre golpes deja subir la ganancia para volver a
aplastarla enseguida, y eso se oye como castañeo.

**Umbral automático.** El umbral sigue el nivel de la propia música con un
seguidor que sube en 300 ms y baja en 1,2 s, y el mando pasa a ser un desfase
respecto a ese nivel. Con él, la misma configuración hace el mismo trabajo en
una toma floja y en una fuerte: está medido, no prometido.

**Sidechain externo** con paso alto y paso bajo propios y botón LISTEN para oír
exactamente lo que oye el detector. Sin él, un bombo con subgrave dispara el
compresor por lo que no se escucha.

**Lookahead** de hasta 20 ms: retrasa el audio para que la ganancia pueda moverse
antes de que llegue el pico. Introduce latencia, que el host compensa. La ruta
seca de la mezcla paralela lee la señal **también retrasada**, porque mezclar un
wet retrasado contra un dry sin retrasar es un filtro de peine.

**Link estéreo continuo**, de dos compresores independientes a una sola ganancia
para los dos canales. Por debajo del 100 % un lado fuerte puede tirar de la
imagen, que a veces es justo lo que quieres.

**Los tres automatismos** en la misma fila, porque pertenecen a tres controles
distintos y meterlos dentro de cualquier grupo sería mentir sobre lo que hacen:
THR (umbral automático), REL (el release se adapta a la profundidad de la
reducción en vez de obedecer al mando) y GAIN (devuelve lentamente, en 1,5 s, la
reducción media; lento a propósito, porque un auto gain rápido deshace la
compresión que compensa).

**Mandos gordos y mandos finos.** Threshold, ratio, attack y release son knobs;
knee, range, lookahead y hold son barras finas. Un plugin con doce knobs es un
plugin que no se lee: lo que se toca en cada pista no puede pesar lo mismo que
lo que se ajusta una vez.

### La pantalla

A la izquierda, un historial que se desplaza: nivel de entrada como relleno,
reducción de ganancia colgando desde arriba y el umbral como una línea que se
puede **arrastrar directamente**. A la derecha, la curva estática con un punto
que marca dónde está la señal ahora mismo.

Las dos mitades responden preguntas distintas. El historial dice si está
trabajando y si respira con la música; la curva dice qué le va a pasar a un pico
3 dB más fuerte que este. Un compresor con solo un medidor de reducción te
obliga a deducir las dos.

### Coste de CPU

Entre un 0,48 % y un 0,62 % de un núcleo según la configuración: entre 160 y 207
instancias por núcleo. Lo más caro es desenlazar el estéreo (dos detectores en
vez de uno), y el lookahead apenas se nota.

### Presets

23 presets en seis categorías: Voz, Batería, Bajo, Bus, Instrumentos y Efecto.
Están escritos como puntos de partida, no como sonidos terminados: el umbral
correcto depende de lo alto que venga ya el material y ningún preset puede saber
eso. La descripción de cada uno dice qué tocar primero.

### De la lista de Pro-C 3, lo que queda fuera y por qué

- **Modos Character con saturación (Tube, Diode, Bright) y Drive.** Es
  exactamente la opción de "carácter" que se descartó al elegir un compresor
  limpio con estilos. Cabe añadirla después como sección opcional sin tocar el
  motor: sería una etapa de saturación detrás del elemento de ganancia.
- **Surround y Dolby Atmos hasta 9.1.6.** Toda la suite es estéreo. Soportar
  9.1.6 no es un mando más, es otra arquitectura de buses y de link.
- **Oversampling hasta 32x.** Con 2x ya no hay alias audible en la modulación de
  ganancia; 32x en un compresor es una cifra de folleto.
- **Sincronía con el tempo del host** para el release. Útil en un delay, dudoso
  en un compresor: el release que suena bien depende del material, no del BPM.
- **VST2.** Steinberg ya no concede licencias nuevas.

### Fuera del alcance, a propósito

Fase lineal, spectral dynamic, EQ match, lista de instancias con detección de
solapamiento, saturación de carácter y formatos envolventes. "Fase mínima, cero
latencia" es una decisión de diseño defendible, no una carencia.

---

## Nodo Limit: qué hace

**Un limitador no es un compresor con el ratio arriba.** Un compresor reacciona
al pico cuando ya ha llegado y deja pasar sus primeras muestras; eso está bien
para moldear dinámica y no sirve cuando el trabajo es "de aquí no sale nada por
encima del techo". Así que el audio se retrasa y la ganancia se calcula desde el
futuro:

```
ganancia necesaria  ->  mínimo deslizante sobre la ventana de lookahead
                    ->  envolvente de release (baja instantánea, sube exponencial)
                    ->  dos medias móviles (una rampa sin esquinas)
                    ->  mínimo de seguridad contra el mínimo deslizante
```

El **mínimo deslizante** es lo que hace que la ganancia llegue antes: un pico en
el instante p tira de la ganancia desde p, mientras que el audio de p no sale
hasta p + lookahead. Cuando sale, la ganancia ya ha bajado del todo, suavemente,
y el pico aterriza exactamente en el techo en vez de por encima. Está
implementado como cola monótona, coste constante por muestra: la versión
ingenua —recorrer la ventana en cada muestra— convierte 20 ms de lookahead a
96 kHz en dos mil comparaciones por muestra.

Después de todo eso hay un **clipper duro en el techo que no debería actuar
nunca**, y `getSafetyClipCount()` dice si actuó. Todos los tests de techo exigen
que se quede en cero: esa es la diferencia entre un limitador y un clipper con
buenos modales. Encontró un problema real, además — el margen de un ulp en coma
flotante, que ahora se descuenta al calcular la ganancia.

**True peak** midiendo la señal cuatro veces sobremuestreada, que es como la va a
reconstruir un conversor y como lo define BS.1770. La latencia del detector se
suma al retardo del audio para que el pico siga alineado con su muestra. Hay un
test con una señal cuyos picos entre muestras superan a los picos de muestra: sin
true peak se va por encima del techo, con true peak no.

**Medición de sonoridad** completa: momentánea (400 ms), corto plazo (3 s) e
integrada con las dos puertas del estándar, la absoluta a −70 LUFS y la relativa
10 LU por debajo de la media. Las puertas son el motivo de que la cifra sirva:
sin ellas, el silencio de un tema arrastra su propia medida hacia abajo. El
integrado se acumula en un histograma de 0,1 LU, no en una lista de bloques —
que es lo que hace la implementación de referencia y lo único que evita que un
medidor se convierta en una fuga de memoria en el hilo de audio.

**Cinco estilos**, que cambian qué fracción de la ventana se dedica a suavizar
la entrada en reducción, el release y cuánto trabajo se lleva una segunda etapa
más lenta. Punchy usa menos de la mitad de la ventana: esquina más marcada, el
transitorio sobrevive y se nota que hay un limitador. Es el trato que ese estilo
es.

**Link separado para transitorios y para release**, filtro de DC, disparo por
sidechain externo, **modo delta** para oír lo que se está quitando, y dither TPDF
de 16 o 24 bits para cuando es el último plugin antes del archivo.

**Objetivo de sonoridad.** Un selector con los destinos reales (−9 club, −14
Spotify y YouTube, −16 Apple Music, −23 EBU R128) y un lector de cuánto falta
para llegar, calculado sobre el integrado. El botón SET GAIN suma esa diferencia
a la ganancia: un solo paso, porque el limitador se come parte de cualquier
subida y prometer que llega de una vez sería mentir más que pedir un segundo
clic.

**Bypass igualado en sonoridad.** El bypass devuelve la señal seca subida por la
ganancia de entrada menos la reducción media, así que comparar con y sin
limitador es una comparación de sonido y no de quién suena más fuerte — que es
la comparación que un limitador gana siempre por defecto. El motor sigue
funcionando mientras está en bypass para que esa cifra esté al día.

**Contador de clips de seguridad** en pantalla, junto al true peak. Debe marcar
cero siempre: es la promesa del plugin convertida en un número visible.

No hay control de oversampling, a propósito: la razón por la que un limitador
sobremuestrea son los picos entre muestras, y de eso ya se encarga el detector a
4x. Lo que quedaría es el alias de la propia modulación de ganancia, y esta
ganancia está suavizada por dos medias móviles precisamente para que no se mueva
lo bastante rápido como para generar nada que quitar.

---

## Nodo Ess: qué hace

Un crossover **Linkwitz-Riley de cuarto orden** parte la señal en dos. La mitad
de arriba es lo que vigila el detector, y la ganancia que sale de ahí se aplica
o a esa mitad sola o a toda la señal, según el modo.

El orden importa: cuarto orden es el que hace que las dos mitades vuelvan a
sumar una magnitud plana. **Un de-esser que colorea la señal cuando no hay
sibilancia no es un de-esser, es un control de tono que se mueve**, y el test lo
comprueba a lo largo de toda la banda antes de comprobar ninguna otra cosa.

**Los dos modos fallan en direcciones opuestas**, y por eso el plugin ofrece la
elección en vez de decidir por ti:

- **Banda dividida** baja solo lo que hay por encima del corte. Deja el cuerpo
  de la voz intacto y, forzado, se lleva el aire con la sibilancia y empieza a
  cecear.
- **Banda ancha** baja toda la señal mientras dura la ese. Conserva el timbre y,
  forzado, agacha la voz entera en cada S.

Medido: con una ese fuerte y un tono de cuerpo a la vez, el modo dividido deja
el cuerpo a 0,02 dB de donde estaba y el ancho se lo lleva 15,7 dB abajo.

**Umbral adaptativo.** El umbral cabalga sobre un seguidor lento de la propia
banda, así que el mando pasa a ser una distancia por encima del nivel del
material y no un nivel fijo. Con umbral fijo, la misma configuración hace
10 dB menos de trabajo en una toma 20 dB más floja; con el adaptativo, la
diferencia es de una décima. Y la pantalla dibuja **dónde ha acabado el umbral**,
no dónde está el mando: un umbral que se mueve y no se ve es un umbral en el que
no se puede confiar, y es la razón por la que mucha gente abandona los de-essers
adaptativos.

**Ratio infinita con rango.** La banda se devuelve al umbral y el rango dice
hasta dónde se le permite llegar. Un de-esser no está moldeando dinámica, está
quitando un sonido concreto de en medio; el rango es el control que evita que
una ese acabe en ceceo.

**La sibilancia es una banda, no todo lo que hay por encima de un corte.** Con
el detector abierto hasta arriba, un plato o el aire de una sala brillante son
indistinguibles de una ese, y el de-esser agacha la voz cada vez que entra la
batería. El mando **Det. top** cierra el techo de lo que escucha el detector, y
solo el detector: la señal nunca pasa por ese filtro, así que estrechar la
escucha no puede colorear nada. Medido: un tono de 16 kHz mueve el de-esser
17,7 dB con el techo abierto y no lo mueve en absoluto con el techo a 8 kHz, y
en el punto intermedio hace exactamente los dB menos que su propio filtro le
quita — la expectativa del test se deriva de la magnitud del filtro, no de la
cifra de manual, porque la sección digital tiene un cero doble en Nyquist y a
16 kHz sobre 48 kHz eso ya se nota.

**Solo el mid o solo el side.** Una voz principal va en el centro, así que
de-essear solo el mid deja intactas la reverb, los dobles y todo lo que vive en
los lados. El modo side es más quirúrgico y más raro: es para cuando la
sibilancia viene de los dobles abiertos y no de la voz. La rotación entra por la
puerta y sale por la puerta, así que **todo lo de en medio no cambia**: el
crossover corre sobre las dos componentes, no sobre izquierda y derecha. Eso es
lo que mantiene la suma transparente — filtrar solo la componente que se trata
dejaría la otra llegando con otra fase y peinaría la recombinación. El test lo
comprueba muestra a muestra contra el modo estéreo, no solo en energía. Con las
dos componentes separadas el enlace estéreo deja de significar nada, así que el
control se apaga en vez de quedarse encendido sin hacer nada.

**Escuchas**: la banda que vigila el detector, o la diferencia — lo que se está
quitando. Si en la diferencia se oyen palabras, está haciendo demasiado.

---

## Nodo Gate: qué hace

Una puerta no es un aparato distinto de un expansor, es un expansor con una
pendiente lo bastante empinada como para que el rango haga todo el trabajo. Está
escrito como una sola función, y por eso el mando de ratio es continuo en vez de
cambiar de comportamiento al llegar arriba: 2:1 baja otros 10 dB lo que está
10 dB por debajo del umbral, y arriba del todo cierra.

**Histéresis, que es lo que la mayoría de la gente no sabe que le falta.** Una
puerta con un solo umbral se abre y se cierra cada vez que el nivel tiembla
alrededor de él, y en cualquier cosa con decaimiento eso pasa varias veces por
segundo y se oye como castañeteo. Dos umbrales —abre aquí, cierra unos dB más
abajo— no cuestan nada y eliminan el problema entero. La pantalla dibuja **las
dos líneas** y sombrea la banda entre ellas, porque es la forma más rápida de
explicar un control que casi nadie ha usado a conciencia.

**El detector tiene balística propia, y es deliberada.** Una senoide rectificada
cae a cero dos veces por ciclo, así que un detector de pico desnudo lee
"silencio" a mitad de cada ciclo de una nota grave y la puerta castañetea a la
frecuencia de la nota. Rellenar esos valles es para lo que sirve el release del
detector; 15 ms cubre todo hasta unos 30 Hz. El release del mando, que es lo que
se oye, es una etapa aparte.

**Hold** mantiene la ganancia donde está antes de dejarla ir. Es la cura más
barata que existe para el castañeteo: no hagas nada un momento y mira a ver si
la señal vuelve.

**Seis algoritmos**, y como en el compresor todo lo que hacen está a la vista en
`styleTraits()`: multiplicadores de ataque, release y knee, hold añadido,
dependencia del programa y ventana del detector. Medido: de 3 dB a 12 dB de
cierre, Percussive tarda 10 ms, Clean 17, Smooth 44 y Bus 89.

**Ducking** con sidechain externo, que es la misma curva estática del compresor
con el rango puesto: 12 dB por encima del umbral a 4:1 aparta la señal 9 dB
exactos.

**Disparo por MIDI.** La puerta abre con una nota y cierra al soltarla, con el
mismo ataque, hold, release y rango — que es lo que hace que suene a la misma
puerta y no a un interruptor. Cuenta notas en vez de usar una bandera, así que
un acorde no la cierra en cuanto se levanta el primer dedo. Se resuelve una vez
por bloque a propósito: el moldeado lo hacen el ataque y el release, que se
miden en milisegundos, y la precisión de muestra movería la apertura como mucho
un buffer a cambio de partir cada bloque en dos.

**Enlazado, L/R o M/S.** El enlace no es un detalle en una puerta: dos puertas
independientes en un par estéreo abren en momentos ligeramente distintos y la
imagen se mueve. En M/S el detector lee las componentes *antes* de rectificar,
porque la señal de side es la diferencia de las dos ondas, que no es la
diferencia de sus dos envolventes.

**Sin oversampling, a diferencia del compresor.** Una puerta modula ganancia, y
el ruido de banda ancha que genera un cambio de ganancia rápido es señal real,
no alias: sobremuestrear no lo quita. Lo que sí ayuda es el lookahead, que deja
que la puerta esté abierta antes de que llegue el transitorio, y ese sí está.

---

## Nodo Delay: qué hace

Dos líneas, cada una con su tiempo, su sitio en la imagen estéreo y su camino
de vuelta al lazo. **Todo lo interesante de un delay pasa dentro de ese lazo**:
los filtros, la saturación y la degradación están dentro, así que cada
repetición está una generación más lejos que la anterior en vez de estar todas
procesadas lo mismo a la salida. Esa es la diferencia entre un delay que decae
hacia algún sitio y uno que solo se va haciendo más flojo. La pantalla dibuja
la curva del filtro **una vez por generación** justamente para enseñarlo: un
low pass que no parece nada en la primera repetición se ha llevado los agudos
enteros para la quinta.

**El dibujo de las repeticiones.** Dos líneas que se pasan la señal a tiempos
distintos no producen una serie que decae, producen un ritmo, y casi todos los
delays te piden que lo descubras de oído. Aquí está en pantalla: cada
repetición como una barra en un eje de tiempo, arriba la izquierda de la imagen
y abajo la derecha, con la altura que le toca. Se calcula simulando la red de
realimentación como una sucesión de eventos, no corriendo audio, así que es
exacto y cuesta nada. Dos rutas por la red pueden llegar en el mismo instante
—con 120 ms y 200 ms, izquierda-luego-derecha y derecha-luego-izquierda caen
las dos en 320— y cuando pasa, el audio las suma; el dibujo también.

**Cinta o fade.** Son dos respuestas distintas a "el tiempo ha cambiado", y el
interruptor entre ellas es toda la pregunta analógico-contra-digital. En modo
cinta el cabezal de lectura *se desliza* hacia la nueva distancia: como se está
moviendo por el buffer a un ritmo distinto de una muestra por muestra, lo que
sale está desplazado en tono mientras dure el viaje. No es un truco añadido
encima, es lo que hace la geometría. En modo fade salta y se hace un crossfade
de 30 ms: ni glissando ni cambio de tono, las repeticiones simplemente pasan a
sonar con la nueva separación.

**Interpolación Catmull-Rom**, no lineal. Un interpolador lineal es un filtro
paso bajo cuya frecuencia de corte depende de la parte fraccionaria, así que un
delay modulado construido sobre él se oye más apagado y más brillante según el
bamboleo pasa por delante y por detrás de la media muestra: el sonido respira
sin que nadie se lo haya pedido. Cuatro puntos cuestan un puñado de
multiplicaciones y no hacen eso.

**Wow y flutter** con dos senos a frecuencias sin relación en vez de uno solo:
un seno se oye como vibrato, y una cinta no es tan regular.

**Drive con antialiasing por antiderivada (ADAA).** Cualquier saturador genera
armónicos, y cualquier armónico por encima de la mitad de la frecuencia de
muestreo se pliega hacia abajo como algo inarmónico — el filo metálico que hace
que la distorsión barata suene barata. La solución habitual es sobremuestrear,
que dentro de un lazo de realimentación significa correr el lazo varias veces.
ADAA llega casi igual de lejos por el precio de una variable de estado más: en
vez de evaluar la función en la muestra, evalúa su **promedio a lo largo del
segmento** entre esta muestra y la anterior, que es el cociente de diferencias
de la antiderivada. Promediar es un paso bajo, y se aplica *antes* del plegado
en vez de después, que es el único sitio donde un paso bajo puede ayudar.
Medido: el tercer armónico plegado baja a la mitad frente al saturador desnudo.

**Dónde se normaliza la salida del drive** es todo el diseño de ese mando, y no
hay respuesta gratis. Normalizar en cero —dividir por el drive— deja las
señales pequeñas donde estaban pero aplasta lo fuerte y convierte el mando en
un control de volumen apuntando hacia abajo. Normalizar a fondo de escala sube
las señales pequeñas por todo el drive, que dentro de un lazo es una fuga hacia
arriba. Se referencia a −12 dBFS, más o menos donde vive el lazo; cuesta un
empujón a señal pequeña que la forma cerrada acota en exactamente 1,5x.

**Lo-Fi** al revés: ahí el aliasing es el objetivo, así que no se hace nada por
evitarlo y el sample and hold corre a la frecuencia entera con un contador en
vez de remuestrear como es debido. Es lo que suena a un delay de rack de 1985.

**Freeze** retiene lo que hay en el buffer y no deja entrar nada nuevo. Lo que
se reescribe es el contenido intacto, sin el recortador de seguridad en medio,
así que se puede barrer el filtro del lazo sobre un bucle congelado sin
erosionarlo un poco más en cada vuelta.

**Realimentación por encima del 100 %** está permitida a propósito. Lo que no
puede pasar es que el lazo se dispare: un recortador suave dentro de él hace
que una fuga acabe en un tono estable en lugar de en el infinito.

---

## Nodo Delay, fase dos: patrones y modulación

**El tiempo dividido en pasos.** Hasta dieciséis, y el último cae sobre el
tiempo de delay en sí — que es donde el lazo se cierra. Por eso con un solo
paso esto *es* el delay de siempre, bit a bit, y hay un test que lo comprueba.
Los pasos anteriores no han dado la vuelta todavía, así que se leen del buffer
sin pasar por los filtros ni la saturación; el último sí ha pasado. No es una
incoherencia, es dónde está físicamente la etapa de tono: un tap anterior a
ella no ha pasado por ella.

**El patrón se edita arrastrando.** Cada paso es una barra: la altura es el
nivel y la posición dentro de su columna es el pan, así que un gesto pone las
dos cosas y una barra inclinada a la izquierda está panoramizada a la
izquierda. Se lee sin leyenda. Alt-clic quita un paso.

**Normalizado por la división, no por cuántos pasos estén puestos.** Las dos
opciones evitan que dieciséis taps suenen veinticuatro decibelios más que uno,
pero solo esta deja en paz a los demás cuando apagas uno: quitar un paso tiene
que adelgazar el patrón, no subir el resto.

**Matriz de modulación de seis ranuras**, con dos LFOs y un seguidor de
envolvente. Seis y no cincuenta: la mitad de los destinos de una matriz enorme
son cosas que nadie modula, y cada uno es una fila que hay que saltarse para
llegar a la que buscabas. Los destinos son una lista escogida — el tiempo (las
dos líneas o cada una), realimentación, cruce, drive, lo-fi, los dos filtros
del lazo, mezcla, anchura, y el nivel y la apertura del patrón.

**Los moduladores corren por muestra, no por bloque.** Un modulador actualizado
una vez por buffer da escalones, y un escalón sobre un tiempo de delay es un
zipper que se oye. La excepción son las frecuencias de corte de los filtros:
recalcular un biquad cuesta cuatro transcendentales y el lazo tiene cuatro, así
que se recalculan cada 32 muestras — un tercio de milisegundo, muy por debajo
de lo que se puede oír como un salto, y la cuarentava parte de la aritmética.

**Modular el tiempo mueve el cabezal de lectura, no el objetivo.** Es decir:
funciona como el wow, sumándose al desplazamiento, y no como un cambio de
parámetro. Así no dispara el crossfade del modo fade ni el deslizamiento del
modo cinta, y la cantidad es proporcional al tiempo en vez de estar en
milisegundos — el mismo ajuste es un temblor en un slapback y una caída en un
eco de cuatro segundos.

**Los LFOs aleatorios son deterministas.** Un generador congruencial con semilla
fija: dos instancias con los mismos ajustes tienen que sonar igual, o una sesión
no se recupera.

---

## Nodo Verb: qué hace

La reverb se dejó para el final a propósito, porque es el único plugin de la
suite cuya calidad se juzga de oído y aquí no hay oídos. Así que se eligió una
arquitectura en la que **la cosa que la pantalla promete es una cantidad
medible**, y se construyó alrededor de esa idea.

**Una red de retardos realimentada de ocho líneas.** Ocho retardos, una matriz
de mezcla que reparte cada línea entre todas las demás, y un filtro por línea
que decide cuánto vuelve. La matriz es una Hadamard aplicada como transformada
rápida de Walsh-Hadamard: tres etapas de mariposas y una normalización por
1/√8, veinticuatro sumas en total y ninguna multiplicación por coeficiente. Es
ortogonal, así que **ni añade ni quita energía**: todo el decaimiento está en
los ocho filtros. Esa es la propiedad que hace que un tiempo de caída distinto
por frecuencia no sea un efecto colocado encima, sino lo que los filtros *son*.

**Las longitudes son primos**, escalados por la frecuencia de muestreo y por el
tamaño. Primos porque dos líneas cuyas longitudes comparten divisor coinciden
periódicamente y esa coincidencia se oye como un tono en la cola.

**El diseño del filtro sale de la fórmula de la caída, no de un mando.** Para
una línea de L muestras que tiene que caer 60 dB en T segundos, cada vuelta
tiene que perder exactamente −60·L/(T·fs) decibelios. Con esa cifra en la mano,
el low shelf y el high shelf de la línea se diseñan para que la pérdida por
vuelta en graves y en agudos sea la que pide la curva dibujada. `DecayCurve` es
la definición única: la leen el motor, la pantalla y los tests, así que no
existe la posibilidad de que uno de los tres tenga otra idea.

**El tamaño es independiente del decaimiento.** En una sala real no lo es —una
sala grande tarda más en apagarse— y precisamente por eso es útil separarlos:
una sala pequeña y brillante con una cola larga no existe, y es a menudo lo que
pide una mezcla.

**Cuatro allpass de Schroeder por canal a la entrada**, con el coeficiente
limitado a ±0,85. Difuminan el ataque antes de que llegue a la red. A cero se
oyen las primeras repeticiones como repeticiones; arriba el ataque llega como
una nube.

**El movimiento no es opcional del todo.** Ocho retardos fijos resuenan en ocho
frecuencias fijas y la cola se vuelve metálica; el mando decide cuánto más allá
de ese mínimo se mueven las líneas, no si se mueven.

**Los filtros de entrada están antes de la red, no después.** Quitar el grave a
la salida adelgaza una cola que ya está embarrada; quitarlo a la entrada hace
que esa energía nunca haya estado dentro. Es el control que evita que una reverb
larga convierta una mezcla en barro.

**El EQ posterior va después de la reverb y antes de la mezcla**, y se dibuja
sobre la misma imagen que la curva de caída. Dos curvas en un dibujo porque
responden a las dos mitades de la misma pregunta: cuánto dura el grave y cuánto
suena el grave. Acortar la cola de los graves y bajar los graves no son la misma
reparación, y la mayoría de las reverbs solo ofrecen la segunda.

**El ducking lee la entrada seca, no la mojada.** Empuja la cola hacia abajo
mientras la fuente suena y la deja volver en los huecos, que es lo que necesita
una reverb larga sobre una voz para no comerse las palabras. La profundidad
depende del nivel: el detector lee 1 a partir de −6 dBFS y baja de ahí. Medido
con el mando al 90 % y una fuente que llega arriba del todo, la cola baja
20,0 dB — exactamente los 20·log₁₀(1 − 0,9) que promete el mando.

**El freeze es exacto.** Cada línea devuelve exactamente lo que recibió —
ganancia uno, shelves en paso directo — y la inyección se corta, así que no
entra nada nuevo. Trece segundos después la cola ha cambiado 0,01 dB.

**No hay limitador dentro del lazo.** Con una matriz ortogonal y una ganancia
por línea menor que uno la red es estable por construcción, y un limitador ahí
dentro solo serviría para tapar un error de diseño en vez de arreglarlo. Lo
único que queda es una guarda contra valores no finitos, y un barrido de
cuarenta ajustes aleatorios que comprueba que ninguna esquina del espacio de
parámetros produce una ganancia mayor que uno.

**La cola declarada al host son 25 segundos.** Un plugin que declara menos de lo
que dura se queda con la cola cortada en un bounce, que es justo el sitio donde
la cola era todo el asunto.

### Lo que queda fuera, a propósito

- **Reflexiones tempranas.** Un juego de reflexiones separado suena a sala
  concreta y no a espacio; la difusión de entrada hace el trabajo que se le pide
  aquí sin traerse una sala ajena a la mezcla.
- **El barrido de carácter** de un solo mando. Suena bien en una demo y hace
  imposible saber qué se está cambiando; los tres controles que hay debajo
  —difusión, movimiento y tamaño— son los mismos parámetros con nombre propio.
- **Oversampling.** No hay nada no lineal en la señal: la red es lineal de
  principio a fin.

---

## Licencias

- **JUCE 9** en el tramo Starter (gratuito). `JUCE_DISPLAY_SPLASH_SCREEN=1`
  sigue activado en `cmake/NodoPlugin.cmake`, aunque **JUCE 9 ya no dibuja
  ninguna pantalla de bienvenida y avisa de que la marca se ignora**. Se deja
  puesta porque no cuesta nada y porque era lo que pedía el tramo gratuito en
  JUCE 7 y 8. **Conviene leer el EULA vigente antes de publicar** y confirmar
  las condiciones exactas del tramo gratuito.
- **VST3 SDK** de Steinberg: gratuito aceptando su licencia propietaria, con
  registro como desarrollador.
- **AAX**: fase 2. Requiere registro de desarrollador con Avid, build de
  desarrollo de Pro Tools e iLok con herramientas de firma PACE.
