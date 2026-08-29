# Probar los plugins de Nodo en el Mac sin gastar nada

Todo lo de esta guía es gratis. No hace falta firmar los plugins ni pagar la
cuota de Apple: **lo que compilas en tu propio Mac no lleva la marca de
cuarentena** que macOS le pone a lo que se descarga de internet. El `xattr`
del archivo de instalación es solo para la gente que se descargue el ZIP.

---

## 1. Compilarlo (una vez, ~10 min la primera)

Necesitas dos cosas, las dos gratuitas:

```bash
xcode-select --install     # Command Line Tools de Xcode
brew install cmake         # o el .dmg de cmake.org si no usas Homebrew
```

Después, dentro de la carpeta del proyecto:

```bash
./build_mac.sh
```

El script comprueba las herramientas, descarga JUCE si falta, compila en
Release universal (Apple Silicon + Intel), pasa los 382 tests de DSP y te
dice dónde ha quedado cada formato. Al terminar tendrás:

| Formato | Ruta |
|---|---|
| VST3 | `~/Library/Audio/Plug-Ins/VST3/` — EQ, Comp, Limit, Ess, Gate, Delay y Verb |
| AU | `~/Library/Audio/Plug-Ins/Components/` — los siete |
| App suelta | `build/plugins/<plugin>/<plugin>_artefacts/Release/Standalone/` |

Se copian solos: `COPY_PLUGIN_AFTER_BUILD` está activado. Cada vez que
recompiles, se sobrescriben.

---

## 2. Reaper

`Preferences > Plug-ins > VST > Re-scan`. Aparecen como **SonidoenRed: Nodo EQ**,
**Nodo Comp**, **Nodo Limit**, **Nodo Ess**, **Nodo Gate**, **Nodo Delay** y
**Nodo Verb** (VST3), y también en la lista de Audio Units.

Prueba **las dos versiones**: son el mismo DSP pero envoltorios distintos, y
los bugs de estado y de automatización casi siempre salen en uno solo.

Para trabajar con señal controlada, todo con lo que ya trae Reaper:

- **JS: Signal Generator** — seno, ruido blanco y rosa.
- **JS: Frequency Spectrum Analyzer Meter** — para ver la curva medida.

Pon el generador en ruido rosa, Nodo EQ detrás y el analizador al final:
la curva que mide el analizador tiene que coincidir con la que dibuja el
plugin. Si coinciden, el DSP y el dibujo están de acuerdo.

## 3. La app suelta

Doble clic en `Nodo EQ.app` o `Nodo Comp.app`. Sirve para mirar la interfaz y trastear sin
abrir el DAW; elige entrada y salida en `Options > Audio Settings`. Para
juzgar el sonido de verdad, mejor en Reaper con material real.

---

## 4. Qué probar, por orden de importancia

Esto es exactamente lo que yo no he podido comprobar aquí, ordenado por
lo que más duele si falla:

1. **Clics al mover los mandos.** Mueve FREQ y GAIN rápido, con música
   sonando. Los coeficientes se recalculan cada 32 muestras con la
   frecuencia suavizada en log2; si oyes zipper o chasquidos, es ahí.
2. **Cambiar el tipo de filtro y el modo de canal (Stereo/M/S/L/R) en
   caliente**, con señal pasando. No debería dar ningún salto.
3. **Digital vs Analog.** Pon un high shelf en 12–16 kHz con Q alto y
   alterna. En Analog la campana no se estrecha al acercarse a Nyquist:
   esa diferencia es el trabajo de compensación de cramping. A 44.1 kHz
   se nota; a 96 kHz casi nada.
4. **Bandas dinámicas.** Baja el THRESHOLD hasta que el lector de GR se
   mueva. Comprueba que ATTACK y RELEASE se sienten como esperas y que
   el número de GR sigue al material.
5. **Oversampling 2x.** Actívalo y haz un null test (ver abajo): si
   Reaper compensa la latencia correctamente, el resultado es silencio.
6. **Bypass.** Debe hacer crossfade, no cortar. Actívalo con música
   fuerte sonando.
7. **Guardar y recargar.** Monta una curva rara, guarda el proyecto,
   cierra Reaper, ábrelo. Tiene que volver todo: bandas, presets, slot
   A/B, tamaño de la ventana.
8. **Automatización.** Escribe automatización de FREQ de una banda y
   reprodúcela. Es el camino más rápido para encontrar ruido en el
   suavizado.
9. **Presets y A/B.** Los 21 de fábrica, el guardar uno tuyo, el copiar
   A a B.
10. **CPU.** Cmd+Alt+P en Reaper. Aquí midiendo dio 0,26 % de un núcleo
    con 8 bandas estáticas del EQ, 3 % con 24 dinámicas, y un 0,4 % el
    compresor; si en tu Mac se dispara, quiero saberlo.

### Del compresor en concreto

11. **Sidechain externo.** En Reaper: en la pista de música, Nodo Comp; en el
    routing de la pista de voz, un envío al canal 3/4 de la pista de música.
    En el plugin, Sidechain > External. La música se aparta sola. Si eliges
    External y no has enrutado nada, el plugin te lo dice en pantalla.
12. **LISTEN** con los filtros de sidechain: tienes que oír exactamente lo que
    el detector está oyendo.
13. **Lookahead** y null test: con lookahead a 5 ms y ratio 1:1 el resultado
    debe anularse contra la pista seca si el DAW compensa la latencia.
14. **Los cinco estilos** con el mismo material y los mismos mandos. Opto y Bus
    deben apretar menos que Clean; si no, algo va mal en la realimentación.
15. **Arrastrar la línea de umbral** en el display.

### Del limitador en concreto

16. **El techo se respeta.** Sube GAIN hasta 12 dB con material real y mira el
    lector de True Peak del panel: no puede pasar del techo. Si lo pasa,
    quiero saberlo inmediatamente, porque aquí no ocurre en ningún test.
17. **True peak on/off** con material ya limitado por otro plugin: con TP
    activado el pico medido debería quedar por debajo del techo, y con TP
    apagado puede asomarse.
18. **La medición LUFS** contra un medidor que ya uses (el de Reaper, Youlean,
    el de tu DAW). El integrado debería coincidir dentro de un par de décimas.
19. **DELTA** para oír qué está quitando. Es la forma más rápida de decidir
    cuánto GAIN es demasiado.
20. **Los cinco estilos** con el mismo material: Punchy tiene que dejar pasar
    más transitorio que Transparent, y notarse más.

### Del de-esser en concreto

21. **Que no haga nada cuando no hay sibilancia.** Ponlo en una voz con el
    rango a cero y compara en null test contra la pista seca: debe anularse.
22. **Listen band** para encontrar dónde vive la sibilancia de esa voz en
    concreto, antes de tocar el umbral.
23. **Listen difference**: si oyes palabras enteras y no solo eses, el rango o
    el umbral están pasados.
24. **Adaptativo contra fijo** en una toma con partes flojas y fuertes. Con el
    adaptativo debería trabajar parecido en las dos.
25. **Dividido contra ancho** en la misma voz. El ancho conserva mejor el
    timbre y agacha la voz; el dividido no la agacha y puede cecear.
26. **Det. top** sobre una voz con platos o bus de batería de fondo. Con el
    techo abierto el de-esser se dispara con los platos; bájalo a 11–12 kHz y
    debería dejar de hacerlo sin que la ese se le escape. Comprueba de paso que
    bajarlo **no oscurece el sonido**: con el rango a cero, mover ese mando no
    debe cambiar nada de lo que oyes.
27. **Mid only** en una voz con reverb estéreo o dobles abiertos. La cola de
    reverb y los lados tienen que quedarse donde estaban; solo se mueve el
    centro. Y en null test con rango a cero, el modo mid tiene que anularse
    contra el modo estéreo.

### De la puerta en concreto

28. **Que no castañetee.** Ponla en una caja con derrame y sube el umbral hasta
    el punto justo donde empieza a fallar. Ahí, sube la histéresis: el
    castañeteo tiene que desaparecer sin tocar nada más. Es la prueba que
    justifica que ese mando exista.
29. **Lookahead sobre la caja.** Con ataque a 0,1 ms y lookahead a 0, escucha si
    se come el filo del golpe. Súbelo a 1–2 ms y compara. Debería recuperarlo.
30. **Rango corto contra rango largo** en toms. Con el rango a tope suena a
    cinta parada; con 12–18 dB suena a batería.
31. **Sidechain filtrado**: en la caja, sube el HP del sidechain hasta que el
    bombo deje de abrirla. Usa LISTEN para oír la decisión mientras lo haces.
32. **Ducking con sidechain externo**: voz a la entrada de sidechain, música en
    el plugin. Con release largo tiene que volver sin que se note.
33. **Trigger MIDI**: manda notas a la pista y comprueba que la puerta abre con
    ellas. En Reaper hay que enrutar MIDI a la pista; si el DAW no te ofrece la
    entrada MIDI del plugin, quiero saberlo, porque eso es cosa del envoltorio.
34. **Expansor en vez de puerta**: ratio 2:1 y rango 8 dB en una voz con ruido
    de fondo. Debería bajar el fondo sin que se oiga ninguna decisión.

### Del delay en concreto

35. **Sincronía real.** Pon el proyecto a un tempo raro (por ejemplo 93 BPM),
    activa SYNC y comprueba con el metrónomo que las repeticiones caen donde
    deben. Cambia el tempo con el transporte parado y con él andando.
36. **Tape contra Fade** con una nota sostenida sonando: mueve TIME L. En Tape
    la cola tiene que doblarse de tono mientras el cabezal viaja; en Fade no.
    Es la diferencia que más se nota de todo el plugin.
37. **El dibujo contra el oído.** Sube CROSS a la mitad con dos tiempos
    distintos y compara el patrón de barras con lo que oyes. Aquí coinciden
    muestra a muestra en los tests, pero quiero saber si a ti te cuadra.
38. **Feedback al 110 %** con el LOOP LP a media banda. Tiene que sostenerse y
    quedarse en un tono, nunca dispararse. Si te revienta los altavoces quiero
    saberlo inmediatamente, porque aquí no pasa en ningún test.
39. **FREEZE** con un acorde dentro: no debe entrar nada nuevo y el bucle no
    debe decaer. Barre el LOOP LP con la puerta congelada — se filtra lo que
    oyes sin erosionar el bucle.
40. **DRIVE y LO-FI** subiendo poco a poco con feedback alto: cada repetición
    tiene que estar más deshecha que la anterior, no todas igual.
41. **Ping-pong** en unos auriculares: la primera repetición a un lado, la
    segunda al otro, limpio.
42. **Mix a 0 %** y null test contra la pista seca: silencio absoluto.
43. **Latencia**: el delay no reporta latencia (no la tiene). Comprueba que un
    null test con MIX a 0 se anula sin que el DAW compense nada.

### De la fase dos del delay

44. **Abrir un proyecto guardado antes de la fase dos.** Tiene que sonar
    exactamente igual. Aquí hay un test que lo comprueba bit a bit, pero el caso
    real —un proyecto tuyo, guardado por Reaper— solo lo puedes probar tú.
45. **Arrastrar los pasos** en el carril del patrón: arriba y abajo el nivel, a
    izquierda y derecha el pan. Alt-clic para quitar uno. Comprueba que el
    dibujo de repeticiones de arriba responde a lo que haces.
46. **STEPS a 1** con multi-tap encendido: tiene que sonar idéntico a tenerlo
    apagado, salvo por el pan, que ahora lo lleva el paso.
47. **Un patrón con huecos** sincronizado al tempo, sobre una batería. Es la
    prueba de si esto sirve para música o solo para demos.
48. **Coro**: carga el preset. Dos LFOs a noventa grados sobre las dos líneas.
    Si se mueve hacia dentro y hacia fuera en vez de por la imagen, la fase de
    uno de los dos no está entrando.
49. **Modular el tiempo con onda cuadrada**: tiene que saltar de tono, no hacer
    un clic. Y con seno, doblarse en las dos direcciones.
50. **El seguidor de envolvente** sobre la mezcla, con cantidad negativa (el
    preset "Delay que se aparta"). Con una voz, el delay tiene que abrirse en
    los huecos y esconderse mientras habla.
51. **Automatización de un parámetro nuevo** desde el DAW: coge cualquier tap
    level y escribe automatización. Los identificadores son nuevos, y quiero
    saber si algún host se atraganta con ciento y pico parámetros.

### De la reverb

Esta es la que más te toca a ti. Todo lo demás de la suite se puede verificar
midiendo; la reverb tiene una parte —si suena a espacio o suena a lata— que solo
se juzga escuchando, y aquí no hay oídos.

52. **Lo primero, y sin mirar la pantalla**: una caja sola con el preset de sala
    y el de placa. ¿Suena a espacio o suena a metal? Si zumba en una frecuencia
    concreta, sube MOTION y mira si se va. Ahí es donde está el límite de ocho
    líneas, si es que se nota.
53. **DECAY a 10 s sobre un acorde de piano, en solitario.** La cola tiene que
    apagarse suave y sin escalones. Un escalón a mitad de la caída significa que
    una línea se apaga antes que las demás.
54. **Arrastra los dos tiradores de la curva** hasta los extremos: grave el
    doble, agudo a la mitad, y al revés. Lo que dice la pantalla en segundos
    está medido y coincide; lo que quiero saber es si *se oye* lo que dice.
55. **SIZE al mínimo y al máximo con el mismo DECAY.** Debe cambiar el carácter
    —de cabina a nave— sin que la cola dure más ni menos. Es lo contrario a lo
    que hace una sala real y es a propósito.
56. **FREEZE sobre un acorde**, y luego toca encima. Lo congelado no se mueve ni
    un decibelio y lo nuevo no entra. Suéltalo y la cola tiene que seguir desde
    donde estaba, sin salto.
57. **DUCK al 60 % sobre una voz con la reverb larga.** Tiene que apartarse
    mientras canta y volver en los huecos. Si respira o bombea, los tiempos de
    ATTACK y RELEASE son los que hay que mover, no el DUCK.
58. **Render con cola.** Pon un DECAY de 10 s, una nota al final del proyecto y
    exporta. El plugin declara 25 segundos de cola al host: si el bounce corta
    la cola, es que Reaper o Pro Tools no está respetando esa declaración y
    quiero saberlo.
59. **Compatibilidad en mono.** Súmalo a mono con WIDTH al 100 %: la cola tiene
    que seguir ahí. Si se adelgaza mucho al colapsar, la decorrelación entre los
    dos lados está pasada de vuelta.
60. **Cuántas instancias caben.** Ocho líneas con dos shelves cada una es el
    plugin más caro de la suite. Mete diez en un proyecto y mira el medidor de
    CPU de Reaper: quiero un número real de tu máquina, no del contenedor.

### Null test (30 segundos, encuentra cosas que el oído no)

Duplica la pista. En una, Nodo EQ en bypass; en la otra, Nodo EQ con todo
plano. Invierte la fase de una (`JS: Invert Phase` o el botón de fase).
Debe dar **silencio absoluto**. Si no, hay algo que no debería estar
tocando la señal.

---

## 5. Verificación objetiva, también gratis

**pluginval** (Tracktion, gratis) — el mismo que paso yo aquí en cada
build, pero en tu máquina y contra el AU también:

```bash
# descarga pluginval_macOS.zip de github.com/Tracktion/pluginval/releases
pluginval.app/Contents/MacOS/pluginval --strictness-level 10 \
  --validate "$HOME/Library/Audio/Plug-Ins/VST3/Nodo EQ.vst3"
```

Cámbialo por `Nodo Comp.vst3` y por las rutas de `Components/` para el AU.

**auval** — viene dentro de macOS, valida el Audio Unit:

```bash
auval -v aufx Neq1 Sred     # Nodo EQ
auval -v aufx Ncm1 Sred     # Nodo Comp
auval -v aufx Nlm1 Sred     # Nodo Limit
auval -v aufx Nes1 Sred     # Nodo Ess
auval -v aufx Ngt1 Sred     # Nodo Gate
auval -v aufx Ndl1 Sred     # Nodo Delay
auval -v aufx Nvb1 Sred     # Nodo Verb
```

Si `auval` no lo encuentra, el AU no está bien instalado y Logic tampoco
lo vería.

---

## 6. Pro Tools: hoy no carga

Pro Tools solo acepta AAX. No hay VST3 ni AU que valgan, y los wrappers
que traducen formatos son de pago. Así que de momento **Pro Tools se
queda fuera**, como habíamos decidido.

La buena noticia es que la ruta de desarrollo parece más barata de lo que
calculamos: la cuenta de desarrollador de Avid y el SDK de AAX son
gratuitos, y Avid da una versión **Pro Tools Developer** que sí carga
plugins AAX sin firmar, para probar. La firma de PACE, que es lo que hace
falta para el Pro Tools normal, un desarrollador que lo documentó en
marzo de 2026 dice que le salió gratis por ser un plugin gratuito, con
Avid cubriendo el coste; su único gasto anual fue la cuota de Apple.

Eso hay que confirmarlo caso por caso antes de fiarse, pero cambia el
cálculo: cuando quieras entrar en AAX, el primer paso es registrarte en
developer.avid.com y pedir la Developer build, no pagar nada.

---

## 7. Qué contarme cuando lo pruebes

Con esto puedo arreglar sin verlo:

- El síntoma y **cómo repetirlo** (qué banda, qué valores, qué material).
- Si pasa en VST3, en AU o en los dos.
- Frecuencia de muestreo y tamaño de buffer.
- Si tu Mac es Apple Silicon o Intel.
- Captura de pantalla si es visual.

Y si algo suena bien pero no como esperas, dímelo igual. Los números
aquí ya cuadran; lo que falta es tu oído.
