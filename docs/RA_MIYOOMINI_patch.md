# Flujo para generar RA_MIYOOMINI.patch

El objetivo es comparar tu RetroArch 1.21.0 (fork `Rparadise-Team`) con el código
oficial 1.22.0 y producir un parche externo (`RA_MIYOOMINI.patch`) que añada los
drivers y menús de Miyoo/Trimui al árbol limpio de la versión nueva. El proceso
no necesita contenedores ni entornos especiales: basta con `git`, `rg`, `rsync`,
`python3` y las toolchains ARM que ya usas para compilar.

## 1. Preparación de los clones

```bash
# RetroArch oficial v1.22.0
mkdir -p ~/retroarch/upstream
cd ~/retroarch/upstream
git clone --depth=1 --branch v1.22.0 https://github.com/libretro/RetroArch.git .

# Base común oficial v1.21.0 (se usa para fusionar sin perder cambios nuevos)
mkdir -p ~/retroarch/base
cd ~/retroarch/base
git clone --depth=1 --branch v1.21.0 https://github.com/libretro/RetroArch.git .

# Tu fork basado en 1.21.0
mkdir -p ~/retroarch/miyoo
cd ~/retroarch/miyoo
git clone --depth=1 --branch 1.21.0 https://github.com/Rparadise-Team/RetroArch.git .
```

Puedes reutilizar clones existentes; sólo asegúrate de que el árbol oficial está
sin modificar antes de aplicar el parche. Si no quieres clonar la base 1.21.0 a
mano, el script lo hará automáticamente dentro de un directorio temporal.

## 2. Toolchain recomendada y dependencias

Antes de compilar los `Makefile.*` específicos conviene preparar la toolchain de
Miyoo. Un flujo probado en Linux es el siguiente:

```bash
cd ~/retroarch/toolchains
git clone https://github.com/shauninman/union-miyoomini-toolchain.git
cd union-miyoomini-toolchain
sudo bash support/setup-toolchain.sh   # instala en /opt/miyoomini-toolchain
```

El script crea el sysroot y deja disponibles los binarios
`/opt/miyoomini-toolchain/bin/arm-linux-gnueabihf-*`. Añádelos al `PATH` (o
apunta `TOOLCHAIN_DIR` a esa ruta) antes de invocar los Makefiles. Este
toolchain trae `mbedtls`, pero no incluye OpenSSL: necesitas compilarlo una vez
para satisfacer los `-lssl -lcrypto` que usan los targets con red. Un ejemplo:

```bash
cd ~/retroarch/openssl-src/openssl-1.1.1w
export PATH=/opt/miyoomini-toolchain/bin:$PATH
perl Configure linux-armv4 shared \
  --cross-compile-prefix=arm-linux-gnueabihf- \
  --prefix=/opt/miyoomini-toolchain/arm-linux-gnueabihf/libc/usr \
  --openssldir=/opt/miyoomini-toolchain/arm-linux-gnueabihf/libc/usr/ssl
make -j$(nproc)
sudo make install_sw
```

Tras la instalación verás `libssl.so*` y `libcrypto.so*` dentro de
`/opt/miyoomini-toolchain/arm-linux-gnueabihf/libc/usr/lib/`, lo que permite que
`Makefile.miniflip`, `Makefile.miyooflip*` o `Makefile.plus` se enlacen sin
errores.

## 3. Generar el parche externo

Desde la raíz de este repositorio (que contiene `dist-scripts/make_ra_miyoo_patch.sh`):

```bash
./dist-scripts/make_ra_miyoo_patch.sh \
  --upstream-dir ~/retroarch/upstream \
  --base-dir ~/retroarch/base \
  --miyoo-dir ~/retroarch/miyoo \
  --output ~/retroarch/RA_MIYOOMINI.patch
```

El script localiza automáticamente los archivos y directorios relevantes para
los dispositivos Miyoo/Trimui y, además, fusiona cada fichero con un merge de
tres vías (`upstream 1.22.0` + `base 1.21.0` + `fork 1.21.0`). Así consigue
inyectar los controladores específicos sin borrar las funciones nuevas de
RetroArch v1.22.0. Si aparece un conflicto puntual, el helper intenta resolverlo
de forma heurística: conserva siempre el bloque upstream y añade la parte del
fork sólo cuando detecta las etiquetas Miyoo/Trimui (puedes ampliar el
`--keywords` si introduces macros nuevas). A la búsqueda por palabras clave se
añaden bloques completos que arrastran dependencias (por ejemplo
`audio/audio_driver.h`, `command.[ch]`, `retroarch.h`, `menu/menu_*` o
`gfx/drivers/miyoomini/`) para que el parche incluya todos los drivers, menús y
definiciones que exigen los `Makefile.*`
ARM. La rama por defecto del fork es `1.21.0`, así que el script ya utiliza esa
referencia para no depender de `main`. Si incorporas nuevos drivers sólo debes
volver a ejecutar el comando para actualizar el parche.

### Opciones útiles

- `--extra <ruta>`: añade archivos/directorios que no contienen las palabras
  clave pero que dependan de tus dispositivos (por ejemplo, un shader nuevo).
- `--keywords <regex>`: amplía o restringe las coincidencias que `rg` usa para
  buscar archivos relevantes.
- `--manifest <archivo>`: guarda la lista ordenada de archivos que entrarán en
  el diff. Útil si quieres revisar los cambios manualmente con Meld antes de
  aplicar el parche.
- `--list-only`: genera únicamente la lista (ideal para auditorías rápidas) y
  evita que se cree el `patch`. Puedes combinarlo con `--manifest` para dejar
  un fichero de referencia.
- `--upstream-url`, `--base-url`, `--miyoo-url`, `--upstream-ref`, `--base-ref`,
  `--miyoo-ref`: permiten apuntar a forks o ramas distintas sin tocar el script.
- `--keep-workdir`: conserva el directorio temporal para inspeccionar qué
  archivos fueron copiados antes de crear el parche.

> Consejo: si necesitas averiguar exactamente qué archivos deben fusionarse a
> mano, ejecuta `./dist-scripts/make_ra_miyoo_patch.sh --list-only --manifest
> ~/retroarch/miyoo_manifest.txt ...` y revisa la lista resultante en tu
> herramienta de diff favorita.

## 4. Aplicar el parche en RetroArch 1.22.0

```bash
cd ~/retroarch/upstream
patch -p1 < ~/retroarch/RA_MIYOOMINI.patch
```

A partir de este punto, compila usando los `Makefile.mini`, `Makefile.miniflip`,
etcétera, ajustando las rutas de tus toolchains ARMv7/ARMv8 según cada modelo.
Con la toolchain anterior y OpenSSL instalado se han validado, por ejemplo:

```bash
make -f Makefile.mini -j2
make -f Makefile.miniflip -j2
```

Ambos objetivos se enlazan correctamente contra los controladores Miyoo dentro
de RetroArch v1.22.0. Al tratarse de un parche externo, cópialo donde prefieras
(por ejemplo `~/retroarch/RA_MIYOOMINI.patch`) y elimínalo una vez lo hayas
aplicado si no necesitas conservarlo dentro del repositorio.

## 5. Regenerar después de nuevos cambios

1. Realiza los cambios en tu fork 1.21.0 (o en la rama que uses para Miyoo).
2. Vuelve a ejecutar `./dist-scripts/make_ra_miyoo_patch.sh` con las mismas
   rutas. El archivo `RA_MIYOOMINI.patch` se reescribirá automáticamente.
3. Aplica el parche sobre un clon limpio de RetroArch 1.22.0 y vuelve a
   compilar.

De esta manera mantienes tus dispositivos ARMv7/ARMv8 actualizados con las
mejoras del upstream sin perder los drivers personalizados.
