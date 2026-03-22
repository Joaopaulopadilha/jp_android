# clark.md
# Documentação da Biblioteca Clark - Interface Gráfica para JPLang

## Sobre a linguagem JPLang

JPLang é uma linguagem de programação em português. Alguns pontos importantes da sintaxe:

- Comentários usam `#`
- Strings multilinhas usam `"""`
- Variáveis não precisam de declaração de tipo
- Condicional: `se ... senao`
- Loop: `enquanto ... :`
- Importação de bibliotecas: `importar nome_da_biblioteca`
- Saída no terminal: `saida("texto")`
- A função `tm_sleep(ms)` pertence à biblioteca `tempo`, **não** à biblioteca Clark

## Visão Geral

A biblioteca **Clark** permite criar aplicações desktop com interface gráfica usando HTML, CSS e JavaScript. Ela abre uma janela nativa do sistema operacional com um motor web embutido (WebView2 no Windows), onde o desenvolvedor carrega HTML para construir a interface.

A comunicação entre JPLang e a interface é bidirecional:
- **JP → Interface**: envio de HTML, execução de JavaScript
- **Interface → JP**: eventos disparados pelo JavaScript são recebidos no loop principal

## Importação

```jplang
importar clark
importar tempo  # necessario para tm_sleep, NAO faz parte do clark
```

## Lista de Funções da Biblioteca Clark

| Função | Retorno | Parâmetros | Descrição |
|--------|---------|------------|-----------|
| `clark_criar` | inteiro | (texto, inteiro, inteiro) | Cria uma janela e retorna seu ID |
| `clark_html` | inteiro | (inteiro, texto) | Carrega HTML na janela |
| `clark_arquivo` | inteiro | (inteiro, texto) | Carrega arquivo .html na janela |
| `clark_js` | inteiro | (inteiro, texto) | Executa JavaScript na janela |
| `clark_bind` | inteiro | (inteiro, texto) | Registra função JS como evento |
| `clark_ativo` | inteiro | (inteiro) | Retorna 1 se a janela está aberta |
| `clark_evento` | texto | (inteiro) | Retorna o nome do próximo evento ou "" |
| `clark_valor_js` | texto | (inteiro) | Retorna o argumento do último evento |
| `clark_titulo` | inteiro | (inteiro, texto) | Altera o título da janela |
| `clark_tamanho` | inteiro | (inteiro, inteiro, inteiro) | Altera o tamanho da janela |
| `clark_sem_borda` | inteiro | (inteiro) | Remove barra nativa e injeta barra HTML |
| `clark_fechar` | inteiro | (inteiro) | Fecha a janela |

**Somente estas funções pertencem à biblioteca Clark.** Funções como `tm_sleep`, `saida`, etc. são de outras bibliotecas ou do próprio JPLang.

## Arquitetura

O fluxo básico de uma aplicação Clark é:

1. Criar uma janela com `clark_criar`
2. Carregar HTML com `clark_html` ou `clark_arquivo`
3. Registrar eventos com `clark_bind`
4. Rodar um loop com `clark_ativo`, processando eventos com `clark_evento`

```jplang
# Exemplo minimo de estrutura
janela = clark_criar("Titulo", 800, 600)
clark_html(janela, "<html>...</html>")
clark_bind(janela, "nome_funcao_js")

enquanto clark_ativo(janela):
    tm_sleep(30)  # da biblioteca tempo, nao do clark
    evento = clark_evento(janela)

    se evento == "nome_funcao_js":
        valor = clark_valor_js(janela)
        # processar evento...
```

## Referência de Funções

### clark_criar(titulo, largura, altura) → inteiro

Cria uma nova janela e retorna seu identificador (ID).

- `titulo` (texto): título exibido na barra da janela
- `largura` (inteiro): largura da janela em pixels
- `altura` (inteiro): altura da janela em pixels
- **Retorno**: ID da janela (inteiro)

```jplang
janela = clark_criar("Meu App", 800, 600)
```

---

### clark_html(janela, html) → inteiro

Carrega conteúdo HTML diretamente na janela. Substitui qualquer conteúdo anterior.

- `janela` (inteiro): ID da janela
- `html` (texto): código HTML completo
- **Retorno**: 1 se sucesso, 0 se falhou

```jplang
html = """
<!DOCTYPE html>
<html>
<head>
  <style>
    body { background: #222; color: white; font-family: Arial; text-align: center; padding-top: 50px; }
    button { padding: 12px 24px; font-size: 16px; border: none; border-radius: 8px; background: #4a90d9; color: white; cursor: pointer; }
    button:hover { background: #5aa0e9; }
  </style>
</head>
<body>
  <h1 id="msg">Olá!</h1>
  <button onclick="clicou()">Clique aqui</button>
</body>
</html>
"""
clark_html(janela, html)
```

---

### clark_arquivo(janela, caminho) → inteiro

Carrega um arquivo `.html` do disco na janela.

- `janela` (inteiro): ID da janela
- `caminho` (texto): caminho do arquivo HTML
- **Retorno**: 1 se sucesso, 0 se falhou

```jplang
clark_arquivo(janela, "interface.html")
```

---

### clark_js(janela, codigo) → inteiro

Executa código JavaScript na janela. Usado para atualizar a interface a partir do JPLang.

- `janela` (inteiro): ID da janela
- `codigo` (texto): código JavaScript a executar
- **Retorno**: 1 se sucesso, 0 se falhou

```jplang
clark_js(janela, "document.getElementById('msg').innerText = 'Novo texto'")
clark_js(janela, "document.body.style.background = '#333'")
```

---

### clark_bind(janela, nome) → inteiro

Registra uma função JavaScript que, quando chamada, gera um evento acessível pelo JPLang. O nome registrado se torna uma função global no JavaScript.

- `janela` (inteiro): ID da janela
- `nome` (texto): nome da função JS a registrar
- **Retorno**: 1 se sucesso, 0 se falhou

No HTML, a função pode ser chamada com ou sem argumento:

```html
<!-- Sem argumento -->
<button onclick="minha_funcao()">Clique</button>

<!-- Com argumento (valor acessível via clark_valor_js) -->
<button onclick="enviar(document.getElementById('campo').value)">Enviar</button>
```

No JPLang:

```jplang
clark_bind(janela, "minha_funcao")
clark_bind(janela, "enviar")
```

---

### clark_ativo(janela) → inteiro

Verifica se a janela ainda está aberta. Usado como condição do loop principal.

- `janela` (inteiro): ID da janela
- **Retorno**: 1 se aberta, 0 se fechada

```jplang
enquanto clark_ativo(janela):
    tm_sleep(30)
    # processar eventos...
```

**Importante**: sempre use `tm_sleep(30)` (ou valor similar) dentro do loop para evitar consumo excessivo de CPU.

---

### clark_evento(janela) → texto

Retorna o nome do próximo evento pendente na fila. Retorna texto vazio `""` se não há eventos.

- `janela` (inteiro): ID da janela
- **Retorno**: nome da função JS que foi chamada, ou `""` se não há eventos

Os eventos são gerados quando o JavaScript chama uma função registrada com `clark_bind`.

```jplang
evento = clark_evento(janela)

se evento == "salvar":
    # botão salvar foi clicado
se evento == "cancelar":
    # botão cancelar foi clicado
```

---

### clark_valor_js(janela) → texto

Retorna o argumento passado pelo JavaScript no último evento recebido. Deve ser chamada logo após `clark_evento`.

- `janela` (inteiro): ID da janela
- **Retorno**: valor passado como argumento da função JS, ou `""` se nenhum

Exemplo HTML:
```html
<input type="text" id="nome" />
<button onclick="enviar(document.getElementById('nome').value)">Enviar</button>
```

Exemplo JPLang:
```jplang
clark_bind(janela, "enviar")

enquanto clark_ativo(janela):
    tm_sleep(30)
    evento = clark_evento(janela)

    se evento == "enviar":
        nome = clark_valor_js(janela)
        saida("Nome digitado: " + nome)
```

É possível enviar dados complexos como JSON:
```html
<button onclick="salvar(JSON.stringify({nome: 'João', idade: 25}))">Salvar</button>
```

---

### clark_titulo(janela, texto) → inteiro

Altera o título da janela.

- `janela` (inteiro): ID da janela
- `texto` (texto): novo título
- **Retorno**: 1 se sucesso, 0 se falhou

```jplang
clark_titulo(janela, "Novo Título")
```

---

### clark_tamanho(janela, largura, altura) → inteiro

Altera o tamanho da janela.

- `janela` (inteiro): ID da janela
- `largura` (inteiro): nova largura em pixels
- `altura` (inteiro): nova altura em pixels
- **Retorno**: 1 se sucesso, 0 se falhou

```jplang
clark_tamanho(janela, 1024, 768)
```

---

### clark_sem_borda(janela) → inteiro

Remove a barra de título nativa do sistema operacional e injeta uma barra customizada em HTML com botões de minimizar, maximizar e fechar. A janela pode ser arrastada pela barra customizada. O `body` recebe `padding-top: 32px` automaticamente para não ficar sob a barra.

- `janela` (inteiro): ID da janela
- **Retorno**: 1 se sucesso, 0 se falhou

Deve ser chamada **após** `clark_html` ou `clark_arquivo`.

```jplang
janela = clark_criar("Meu App", 800, 600)
clark_html(janela, html)
clark_sem_borda(janela)
```

A barra customizada tem visual escuro (fundo `#1e1e1e`) com ícones SVG para os três botões. O botão de fechar fica vermelho ao passar o mouse. O título exibido é o conteúdo da tag `<title>` do HTML.

---

### clark_fechar(janela) → inteiro

Fecha a janela programaticamente e libera os recursos.

- `janela` (inteiro): ID da janela
- **Retorno**: 1 se sucesso, 0 se falhou

```jplang
clark_fechar(janela)
```

---

## Exemplos Completos

### Exemplo 1: Olá Mundo

O exemplo mais simples — uma janela com texto.

```jplang
importar clark
importar tempo

html = """
<html>
<body style="background:#1a1a2e;color:white;font-family:Arial;display:flex;justify-content:center;align-items:center;height:100vh;">
  <h1>Ola, Clark!</h1>
</body>
</html>
"""

janela = clark_criar("Ola Mundo", 400, 300)
clark_html(janela, html)

enquanto clark_ativo(janela):
    tm_sleep(30)

saida("Janela fechada!")
```

### Exemplo 2: Contador com Botões

Interface com dois botões que incrementam e resetam um contador.

```jplang
importar clark
importar tempo

html = """
<!DOCTYPE html>
<html>
<head>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body { height: 100vh; background: #1a1a2e; color: white; font-family: Arial; display: flex; flex-direction: column; justify-content: center; align-items: center; gap: 20px; }
    h1 { font-size: 64px; }
    .botoes { display: flex; gap: 12px; }
    button { padding: 12px 28px; font-size: 16px; border: none; border-radius: 8px; color: white; cursor: pointer; }
    .somar { background: #4a90d9; }
    .resetar { background: #e74c3c; }
    button:hover { opacity: 0.85; }
  </style>
</head>
<body>
  <h1 id="valor">0</h1>
  <div class="botoes">
    <button class="somar" onclick="somar()">+ 1</button>
    <button class="resetar" onclick="resetar()">Resetar</button>
  </div>
</body>
</html>
"""

janela = clark_criar("Contador", 400, 350)
clark_html(janela, html)
clark_bind(janela, "somar")
clark_bind(janela, "resetar")

contador = 0

enquanto clark_ativo(janela):
    tm_sleep(30)
    evento = clark_evento(janela)

    se evento == "somar":
        contador = contador + 1
        clark_js(janela, "document.getElementById('valor').innerText = '" + contador + "'")

    se evento == "resetar":
        contador = 0
        clark_js(janela, "document.getElementById('valor').innerText = '0'")

saida("Fim!")
```

### Exemplo 3: Formulário com Entrada de Dados

Recebe texto do usuário e processa no JPLang.

```jplang
importar clark
importar tempo

html = """
<!DOCTYPE html>
<html>
<head>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body { height: 100vh; background: linear-gradient(135deg, #0f0c29, #302b63, #24243e); font-family: 'Segoe UI', Arial, sans-serif; display: flex; justify-content: center; align-items: center; }
    .card { background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.12); border-radius: 20px; padding: 40px; width: 380px; }
    h1 { color: #fff; font-size: 22px; text-align: center; margin-bottom: 24px; }
    input { width: 100%; padding: 14px; border: 1px solid rgba(255,255,255,0.15); border-radius: 12px; background: rgba(255,255,255,0.06); color: #fff; font-size: 15px; outline: none; margin-bottom: 16px; }
    input:focus { border-color: #7c5cfc; }
    input::placeholder { color: rgba(255,255,255,0.3); }
    button { width: 100%; padding: 14px; border: none; border-radius: 12px; background: linear-gradient(135deg, #7c5cfc, #5a3fd6); color: white; font-size: 15px; font-weight: 600; cursor: pointer; }
    button:hover { opacity: 0.9; }
    #resultado { margin-top: 20px; padding: 14px; border-radius: 12px; background: rgba(124,92,252,0.12); color: #c4b5fd; font-size: 14px; text-align: center; min-height: 48px; }
  </style>
</head>
<body>
  <div class="card">
    <h1>Qual seu nome?</h1>
    <input type="text" id="nome" placeholder="Digite aqui..." />
    <button onclick="enviar(document.getElementById('nome').value)">Enviar</button>
    <div id="resultado"></div>
  </div>
  <script>
    document.getElementById('nome').addEventListener('keydown', function(e) {
      if (e.key === 'Enter') enviar(document.getElementById('nome').value);
    });
  </script>
</body>
</html>
"""

janela = clark_criar("Formulario", 450, 400)
clark_html(janela, html)
clark_sem_borda(janela)
clark_bind(janela, "enviar")

enquanto clark_ativo(janela):
    tm_sleep(30)
    evento = clark_evento(janela)

    se evento == "enviar":
        nome = clark_valor_js(janela)
        saida("Nome recebido: " + nome)
        clark_js(janela, "document.getElementById('resultado').innerText = 'Ola, " + nome + "! Bem-vindo!';")

saida("Janela fechada!")
```

### Exemplo 4: Interface com Arquivo HTML Externo

Separa o HTML em um arquivo independente.

```jplang
importar clark
importar tempo

janela = clark_criar("Meu App", 800, 600)
clark_arquivo(janela, "interface.html")
clark_sem_borda(janela)
clark_bind(janela, "acao1")
clark_bind(janela, "acao2")

enquanto clark_ativo(janela):
    tm_sleep(30)
    evento = clark_evento(janela)

    se evento == "acao1":
        dado = clark_valor_js(janela)
        # processar acao1...

    se evento == "acao2":
        # processar acao2...

saida("Fim!")
```

## Padrões e Boas Práticas

### Loop Principal

Sempre inclua `tm_sleep(30)` no loop para não consumir 100% da CPU:

```jplang
enquanto clark_ativo(janela):
    tm_sleep(30)
    evento = clark_evento(janela)
    # ...
```

### Múltiplos Eventos no Mesmo Loop

Uma iteração pode ter vários eventos na fila. Para processar todos, use um segundo loop ou apenas trate cada evento individualmente — o loop principal já cuida de processar um por iteração.

### Atualizar Interface via JavaScript

Use `clark_js` para manipular o DOM:

```jplang
# Alterar texto
clark_js(janela, "document.getElementById('titulo').innerText = 'Novo texto'")

# Alterar estilo
clark_js(janela, "document.getElementById('box').style.background = 'red'")

# Esconder elemento
clark_js(janela, "document.getElementById('painel').style.display = 'none'")

# Mostrar elemento
clark_js(janela, "document.getElementById('painel').style.display = 'block'")

# Adicionar classe CSS
clark_js(janela, "document.getElementById('item').classList.add('ativo')")
```

### Enviar Dados Complexos do JS para JP

Use `JSON.stringify` no JavaScript e trate como texto no JPLang:

```html
<button onclick="salvar(JSON.stringify({nome: 'Maria', email: 'maria@email.com'}))">Salvar</button>
```

```jplang
se evento == "salvar":
    dados_json = clark_valor_js(janela)
    saida("Dados: " + dados_json)
```

### Lógica Somente no JavaScript

Para aplicações onde toda a lógica pode ficar no HTML/JS (como jogos ou calculadoras), o loop do JP apenas mantém a janela aberta:

```jplang
janela = clark_criar("Jogo", 800, 600)
clark_html(janela, html_do_jogo)
clark_sem_borda(janela)

enquanto clark_ativo(janela):
    tm_sleep(30)

saida("Jogo fechado!")
```

## Compilação

A biblioteca Clark é compilada como uma DLL (`.jpd`) a partir do código C++:

```bash
g++ -shared -o bibliotecas/clark/clark.jpd bibliotecas/clark/clark.cpp -O3 -std=c++14 -I bibliotecas/clark/src/webview/core/include -I bibliotecas/clark/src/webview2/build/native/include -static -mwindows -ladvapi32 -lole32 -lshell32 -lshlwapi -luser32 -lversion -ldwmapi
```

### Dependências de Compilação

- Compilador g++ com suporte a C++14
- WebView (fonte em `bibliotecas/clark/src/webview/`)
- WebView2 SDK da Microsoft (em `bibliotecas/clark/src/webview2/`)

### Requisito de Execução

- Windows 10 ou superior (WebView2 Runtime pré-instalado no Windows 11; no Windows 10 pode ser necessário instalar)
