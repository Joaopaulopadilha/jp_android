# JPLang Android

Compilador da linguagem JPLang para Android. Transpila código JPLang para C++ e gera executáveis nativos ou APKs com interface gráfica.

## O que é

JPLang é uma linguagem de programação com sintaxe em português. O `jp_android` é o backend Android do compilador — pega um arquivo `.jp` e produz:

- **Executáveis nativos** que rodam via terminal no dispositivo Android (ARM64 ou x86_64)
- **APKs com interface gráfica** usando WebView, instaláveis como apps normais

O compilador detecta automaticamente a arquitetura do dispositivo conectado (celular real ou emulador) e compila para o alvo correto.

## Como funciona

O pipeline de compilação é baseado em transpilação para C++:

```
arquivo.jp → parser → AST → codegen → arquivo.cpp → clang (NDK) → executável/APK
```

### Modo console

```
jp_android teste.jp
```

1. O parser lê o `.jp` e gera a AST
2. O codegen transpila a AST para C++ puro
3. O clang do NDK compila o C++ para a arquitetura do dispositivo
4. O binário é enviado via `adb` e executado

### Modo APK

```
jp_android apk meuapp.jp
```

1. O codegen gera C++ com funções JNI (nativeGetHtml, nativeOnEvent)
2. O clang compila para uma `.so` (shared library)
3. Uma Activity Java mínima com WebView é compilada via `javac` + `d8`
4. Tudo é empacotado em um APK via `aapt2`, alinhado com `zipalign` e assinado com `apksigner`
5. O APK é instalado e aberto automaticamente no dispositivo

## Exemplos

### Hello World (console)

```
saida("Ola do Android via JPLang!")
```

```
jp_android hello.jp
```

### Variáveis e loops

```
para i em intervalo(0, 5):
    saida("valor: {i}")
```

### Funções

```
funcao somar(a, b):
    retorna a + b

x = somar(10, 20)
saida("Total: {x}")
```

### App com interface gráfica

```
html = """<html>
<body style="background:#222;color:#0f0;display:flex;align-items:center;
  justify-content:center;height:100vh;margin:0;font-family:sans-serif">
<h1>Meu App JPLang</h1>
<button onclick="document.getElementById('r').innerText=jp.evento('click','ola')">Clique</button>
<p id="r"></p>
</body>
</html>"""

graf_html(html)

funcao graf_evento(evento, dado):
    se dado == "ola":
        retorna "Ola do JPLang nativo!"
    senao:
        retorna "Evento recebido"
```

```
jp_android apk meuapp.jp
```

### Bibliotecas nativas

```
importar tempo

saida("Data: " + tm_data_str())
saida("Hora: " + tm_hora_str())
tm_sleep(1000)
saida("Esperou 1 segundo!")
```

## Funcionalidades do codegen

O transpiler C++ suporta:

- `saida` / `saidal` — impressão com e sem newline
- Variáveis inteiras, float, string e booleanas
- Expressões aritméticas (+, -, *, /, %)
- Concatenação de strings com interpolação `"texto {variavel}"`
- Comparações (==, !=, >, <, >=, <=) e lógica (e, ou)
- Condicionais: `se` / `ou_se` / `senao`
- Loops: `enquanto`, `para ... em intervalo()`, `repetir`
- `parar` / `continuar`
- Funções com inferência automática de tipos dos parâmetros
- `entrada("prompt")` — leitura do stdin
- Conversões: `inteiro()`, `decimal()`, `texto()`
- Bibliotecas nativas (estáticas e dinâmicas) via JSON descriptor
- Modo APK com `graf_html()` e `graf_evento()`

## Estrutura do projeto

```
jp_android/
├── jp_android.exe              Compilador
├── setup.exe                   Instalador de dependências
├── src/
│   ├── main.cpp                Entry point (mode_run, mode_build, mode_apk)
│   ├── frontend/
│   │   ├── lexer.hpp           Lexer (tokenizador)
│   │   ├── parser.hpp          Parser (gera AST)
│   │   └── ast.hpp             Definição da AST
│   ├── codegen/
│   │   ├── codegen.hpp         Transpiler principal (C++)
│   │   ├── codegen_saida.hpp   Saída com cores
│   │   ├── codegen_atribuicao.hpp  Atribuição, entrada, conversões
│   │   ├── codegen_funcoes.hpp     Funções com inferência de tipos
│   │   ├── codegen_nativo.hpp      Bibliotecas nativas (dlopen/extern C)
│   │   └── codegen_apk.hpp         Modo APK (JNI + WebView)
│   └── backend_android/
│       ├── linker_android.hpp      Detecção de NDK/SDK/dispositivo
│       └── apk_builder.hpp         Pipeline de geração de APK
├── templates/
│   ├── JPLangActivity.java     Activity WebView (template APK)
│   └── AndroidManifest.xml     Manifest (template APK)
├── bibliotecas/
│   └── tempo/                  Biblioteca de tempo (tm_sleep, tm_hora, etc)
├── exemplos/                   Exemplos em JPLang
├── sdk/                        Android SDK (baixado pelo setup)
├── ndk/                        Android NDK (baixado pelo setup)
└── jdk/                        OpenJDK 17 (baixado pelo setup)
```

## Compilação do compilador

```
g++ -std=c++17 -Wall -Wextra -static -I jp_android -o jp_android.exe jp_android/src/main.cpp
```

## Uso

```
jp_android <arquivo.jp>              Compila e executa no dispositivo (console)
jp_android <arquivo.jp> -arm64       Força ARM64
jp_android <arquivo.jp> -x86         Força x86_64 (emulador)
jp_android build <arquivo.jp>        Compila em output/ sem executar
jp_android apk <arquivo.jp>          Gera APK com interface gráfica
jp_android apk <arquivo.jp> -arm64   Gera APK ARM64
```

## Detecção automática

O compilador detecta automaticamente:

- **Dispositivo**: celular real via USB é priorizado sobre emulador. Se nenhum dispositivo está conectado, inicia o emulador automaticamente.
- **Arquitetura**: consulta `adb shell getprop ro.product.cpu.abi` e compila para ARM64 ou x86_64 conforme o dispositivo.
- **NDK/SDK/JDK**: procura nas pastas relativas ao executável.
- **Bibliotecas nativas**: lê o JSON descriptor, detecta se é estática ou dinâmica, e ajusta a compilação.

## Licença

O código do compilador, templates e bibliotecas JPLang são de autoria própria.

O SDK, NDK e JDK não são distribuídos junto — são baixados pelo `setup.exe` diretamente dos servidores oficiais do Google e Microsoft. Consulte `SETUP.md` para instruções de instalação.
