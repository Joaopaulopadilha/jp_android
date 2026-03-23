# Instalação do Ambiente JPLang Android

O JPLang Android depende de ferramentas externas que não podem ser distribuídas junto com o projeto por questões de licença. O `setup.exe` baixa e configura tudo automaticamente.

## Instalação rápida

```
g++ -std=c++17 -Wall -O2 -static -o setup.exe setup.cpp -lwininet
./setup.exe
```

O instalador baixa e configura todos os componentes necessários automaticamente. O processo leva entre 10 e 30 minutos dependendo da velocidade da internet (são aproximadamente 3 GB no total).

## O que é instalado

| Componente | Tamanho | Origem | Licença |
|---|---|---|---|
| OpenJDK 17 (Microsoft) | ~170 MB | aka.ms/download-jdk | GPLv2 + Classpath Exception |
| Android NDK r27d | ~2.2 GB | dl.google.com | Android SDK License |
| SDK Command-line Tools | ~150 MB | dl.google.com | Android SDK License |
| SDK Build Tools 30.0.3 | ~55 MB | via sdkmanager | Android SDK License |
| SDK Platform android-30 | ~65 MB | via sdkmanager | Android SDK License |
| SDK Platform Tools (adb) | ~10 MB | via sdkmanager | Android SDK License |
| Emulador Android | ~300 MB | via sdkmanager | Android SDK License |
| System Image x86_64 | ~1 GB | via sdkmanager | Android SDK License |

Todos os downloads são feitos diretamente dos servidores oficiais do Google e da Microsoft. Nenhum binário de terceiros é redistribuído no repositório.

## Verificar status

Para ver o que já está instalado sem baixar nada:

```
./setup.exe --check
```

Saída esperada quando tudo está instalado:

```
  Componentes:
  [OK] JDK (OpenJDK 17)
  [OK] NDK (r27d)
  [OK] SDK Build Tools 30.0.3
  [OK] SDK Platform android-30
  [OK] SDK Platform Tools (adb)
  [OK] Emulador Android
  [OK] SDK Command-line Tools
  [OK] Templates APK
```

## Estrutura após instalação

```
jp_android/
├── jdk/
│   └── jdk-17.0.18+8/          OpenJDK 17 Microsoft
├── ndk/
│   └── android-ndk-r27d/        Android NDK
├── sdk/
│   ├── build-tools/30.0.3/      aapt2, d8, zipalign, apksigner
│   ├── cmdline-tools/latest/    sdkmanager, avdmanager
│   ├── platforms/android-30/    android.jar
│   ├── platform-tools/          adb
│   ├── emulator/                Emulador Android
│   └── system-images/           Imagem x86_64 para emulador
├── templates/                   Templates de APK (já inclusos no repo)
└── bibliotecas/                 Bibliotecas nativas JPLang (já inclusas)
```

## Instalação manual

Se preferir instalar os componentes manualmente:

### 1. OpenJDK 17

Baixe de https://learn.microsoft.com/en-us/java/openjdk/download e extraia em `jdk/`.

### 2. Android NDK

Baixe de https://developer.android.com/ndk/downloads e extraia em `ndk/`.

### 3. Android SDK

Baixe as command-line tools de https://developer.android.com/studio#command-line-tools-only e extraia em `sdk/cmdline-tools/latest/`. Depois:

```
set JAVA_HOME=caminho\para\jdk\jdk-17.0.18+8
sdk\cmdline-tools\latest\bin\sdkmanager.bat --sdk_root=sdk "build-tools;30.0.3"
sdk\cmdline-tools\latest\bin\sdkmanager.bat --sdk_root=sdk "platforms;android-30"
sdk\cmdline-tools\latest\bin\sdkmanager.bat --sdk_root=sdk "platform-tools"
sdk\cmdline-tools\latest\bin\sdkmanager.bat --sdk_root=sdk "emulator"
sdk\cmdline-tools\latest\bin\sdkmanager.bat --sdk_root=sdk "system-images;android-30;google_apis;x86_64"
```

### 4. Criar emulador (opcional)

```
sdk\cmdline-tools\latest\bin\avdmanager.bat create avd -n teste_jp -k "system-images;android-30;google_apis;x86_64"
```

## Requisitos do sistema

- Windows 10/11 (64-bit)
- MinGW-w64 com g++ (para compilar o compilador e o setup)
- ~10 GB de espaço em disco
- Conexão com internet (para o setup baixar os componentes)
- Para celular real: cabo USB com depuração USB ativada
- Para emulador: CPU com suporte a virtualização (Intel VT-x / AMD-V)

## Compilação do setup

O setup é um executável C++ standalone que usa WinINet para downloads:

```
g++ -std=c++17 -Wall -O2 -static -o setup.exe setup.cpp -lwininet
```

## Solução de problemas

**setup.exe não baixa nada**: verifique sua conexão com internet e se o antivírus não está bloqueando o WinINet.

**sdkmanager não aceita licenças**: rode manualmente:
```
set JAVA_HOME=caminho\para\jdk
sdk\cmdline-tools\latest\bin\sdkmanager.bat --licenses
```

**Emulador não inicia**: verifique se a virtualização está habilitada na BIOS (Intel VT-x ou AMD SVM).

**adb não encontra dispositivo**: ative a depuração USB no celular (Configurações → Opções do desenvolvedor → Depuração USB) e aceite a autorização no celular quando conectar.

**Erro "JAVA_HOME is set to an invalid directory"**: certifique-se de que o caminho do JDK não contém espaços ou caracteres especiais.
