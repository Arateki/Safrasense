"""
extra_scripts (pre:) do [env:qemu].

Por que isto existe: no build híbrido arduino+espidf, os arquivos de
`src/` são compilados via o modelo de código do CMake (componente idf
"src"), cujo ambiente de compilação (`project_env`) é clonado do `env`
principal ANTES do PlatformIO injetar `PIO_UNIT_TESTING`/`UNIT_TEST`
(isso só acontece depois, quando `ConfigureTestTarget()` roda). Ou seja,
`#ifndef PIO_UNIT_TESTING` em `src/main.cpp` nunca vê essa macro via
`-D`, mesmo com `pio test -e qemu` — build_flags normais (ex.:
QEMU_EMULATOR) chegam a tempo, mas a macro de teste não.

Workaround: gerar um header próprio ANTES do framework rodar (extra_scripts
"pre:" roda cedo o bastante) refletindo se este é um build de teste, e
`src/main.cpp` inclui esse header em vez de depender do define via CLI.
"""

Import("env")

import os

is_test_build = env.get("BUILD_TYPE") == "test"

guard_path = os.path.join(env.subst("$PROJECT_INCLUDE_DIR"), "pio_test_guard.h")
content = "// Gerado por emu/gen_test_guard.py -- não editar manualmente.\n#pragma once\n"
if is_test_build:
    content += "#define PIO_UNIT_TESTING 1\n"

os.makedirs(os.path.dirname(guard_path), exist_ok=True)
with open(guard_path, "w") as fp:
    fp.write(content)
