// clark.cpp
// Biblioteca nativa Clark (Interface Gráfica com WebView) para JPLang 3.0
//
// Cria janelas nativas com conteúdo HTML/CSS/JS usando WebView2 (Windows) ou WebKitGTK (Linux).
// Todas as funções recebem/retornam int64_t. Strings = char* castado para int64_t.
//
// Compilar (Windows):
//   g++ -shared -o bibliotecas/clark/clark.jpd bibliotecas/clark/clark.cpp -O3 -std=c++14 -I bibliotecas/clark/src/webview/core/include -I bibliotecas/clark/src/webview2/build/native/include -static -mwindows -ladvapi32 -lole32 -lshell32 -lshlwapi -luser32 -lversion -ldwmapi
//
// Compilar (Linux / Ubuntu 20.04 Focal Fossa):
//   g++ -shared -fPIC -o bibliotecas/clark/libclark.jpd bibliotecas/clark/clark.cpp -O3 -std=c++14 -I bibliotecas/clark/src/webview/core/include $(pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.0) -lpthread
//
// Dependencias Linux:
//   sudo apt install libgtk-3-dev libwebkit2gtk-4.0-dev

#ifdef _WIN32
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0A00
    #endif
#endif

#define WEBVIEW_STATIC
#include "webview/webview.h"

#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <cstdio>

#ifdef _WIN32
    #include <windows.h>
    #include <dwmapi.h>
#else
    #include <gtk/gtk.h>
    #include <gdk/gdkx.h>
#endif

// =============================================================================
// EXPORT
// =============================================================================
#ifdef _WIN32
    #define JP_EXPORT extern "C" __declspec(dllexport)
#else
    #define JP_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// =============================================================================
// BUFFER ROTATIVO PARA RETORNO DE STRINGS
// =============================================================================
static char str_bufs[8][4096];
static int str_buf_idx = 0;
static std::mutex str_buf_mutex;

static int64_t retorna_str(const std::string& s) {
    std::lock_guard<std::mutex> lock(str_buf_mutex);
    char* buf = str_bufs[str_buf_idx++ & 7];
    strncpy(buf, s.c_str(), 4095);
    buf[4095] = '\0';
    return (int64_t)buf;
}

// =============================================================================
// ESTRUTURAS DE DADOS
// =============================================================================
struct Janela {
    webview::webview* wv;
    std::thread wv_thread;
    std::atomic<bool> ativa;
    std::atomic<bool> pronta;

    // Fila de eventos (binds chamados pelo JS)
    struct Evento {
        std::string nome;
        std::string valor;
    };
    std::queue<Evento> fila_eventos;
    std::mutex eventos_mutex;
    std::string ultimo_valor; // ultimo valor recebido do JS

    // Fila de comandos (JP -> WebView)
    struct Comando {
        enum Tipo { CMD_HTML, CMD_JS, CMD_TITULO, CMD_TAMANHO, CMD_ARQUIVO, CMD_BIND, CMD_FECHAR, CMD_SEM_BORDA };
        Tipo tipo;
        std::string arg1;
        int largura;
        int altura;
    };
    std::queue<Comando> fila_comandos;
    std::mutex comandos_mutex;

    std::string titulo_inicial;
    int largura_inicial;
    int altura_inicial;
    bool sem_borda;

    Janela() : wv(nullptr), ativa(false), pronta(false), largura_inicial(800), altura_inicial(600), sem_borda(false) {}
};

// =============================================================================
// ESTADO GLOBAL
// =============================================================================
static std::unordered_map<int, Janela*> janelas;
static std::mutex janelas_mutex;
static std::atomic<int> proximo_id{1};

// =============================================================================
// THREAD DO WEBVIEW
// =============================================================================
static void webview_thread_func(Janela* jan) {
    try {
        jan->wv = new webview::webview(false, nullptr);
        jan->wv->set_title(jan->titulo_inicial);
        jan->wv->set_size(jan->largura_inicial, jan->altura_inicial, WEBVIEW_HINT_NONE);
        jan->ativa.store(true);
        jan->pronta.store(true);

        // Loop: processa comandos pendentes e roda o webview
        // Usamos dispatch para injetar comandos na thread do webview
        jan->wv->run();

        // Quando sai do run(), a janela foi fechada
        jan->ativa.store(false);
        delete jan->wv;
        jan->wv = nullptr;
    } catch (...) {
        jan->ativa.store(false);
        jan->pronta.store(true);
    }
}

static void processar_comandos(Janela* jan) {
    std::queue<Janela::Comando> cmds;
    {
        std::lock_guard<std::mutex> lock(jan->comandos_mutex);
        std::swap(cmds, jan->fila_comandos);
    }

    while (!cmds.empty()) {
        auto& cmd = cmds.front();
        switch (cmd.tipo) {
            case Janela::Comando::CMD_HTML:
                if (jan->wv) jan->wv->set_html(cmd.arg1);
                break;
            case Janela::Comando::CMD_JS:
                if (jan->wv) jan->wv->eval(cmd.arg1);
                break;
            case Janela::Comando::CMD_TITULO:
                if (jan->wv) jan->wv->set_title(cmd.arg1);
                break;
            case Janela::Comando::CMD_TAMANHO:
                if (jan->wv) jan->wv->set_size(cmd.largura, cmd.altura, WEBVIEW_HINT_NONE);
                break;
            case Janela::Comando::CMD_ARQUIVO: {
                // Lê o arquivo e carrega como HTML
                FILE* f = fopen(cmd.arg1.c_str(), "rb");
                if (f) {
                    fseek(f, 0, SEEK_END);
                    long tam = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    std::string conteudo(tam, '\0');
                    fread(&conteudo[0], 1, tam, f);
                    fclose(f);
                    if (jan->wv) jan->wv->set_html(conteudo);
                }
                break;
            }
            case Janela::Comando::CMD_BIND: {
                std::string nome_bind = cmd.arg1;
                if (jan->wv) {
                    jan->wv->bind(nome_bind, [jan, nome_bind](const std::string& args) -> std::string {
                        // args vem no formato JSON array, ex: ["valor"] ou ["valor1","valor2"]
                        // Extraímos o primeiro argumento removendo aspas e colchetes
                        std::string valor = "";
                        if (args.size() > 2) {
                            // Remove [ e ]
                            std::string inner = args.substr(1, args.size() - 2);
                            // Remove aspas do primeiro argumento
                            if (inner.size() >= 2 && inner[0] == '"') {
                                size_t fim = inner.find('"', 1);
                                if (fim != std::string::npos) {
                                    valor = inner.substr(1, fim - 1);
                                }
                            } else {
                                // Argumento sem aspas (numero etc)
                                size_t fim = inner.find(',');
                                valor = (fim != std::string::npos) ? inner.substr(0, fim) : inner;
                            }
                        }
                        std::lock_guard<std::mutex> lock(jan->eventos_mutex);
                        Janela::Evento ev;
                        ev.nome = nome_bind;
                        ev.valor = valor;
                        jan->fila_eventos.push(ev);
                        return "";
                    });
                }
                break;
            }
            case Janela::Comando::CMD_FECHAR:
                if (jan->wv) jan->wv->terminate();
                break;
            case Janela::Comando::CMD_SEM_BORDA: {
                if (jan->wv) {
#ifdef _WIN32
                    // =========================================================
                    // WINDOWS: Remove barra nativa via Win32 API
                    // =========================================================
                    HWND hwnd = (HWND)jan->wv->window().value();
                    if (hwnd) {
                        // Remove barra de titulo e bordas
                        LONG style = GetWindowLong(hwnd, GWL_STYLE);
                        style &= ~(WS_CAPTION | WS_THICKFRAME);
                        style |= WS_POPUP;
                        SetWindowLong(hwnd, GWL_STYLE, style);

                        LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
                        exStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
                        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);

                        // Sombra com DWM (efeito Aero)
                        MARGINS margins = {1, 1, 1, 1};
                        DwmExtendFrameIntoClientArea(hwnd, &margins);

                        // Aplica as mudancas
                        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                            SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);

                        jan->sem_borda = true;

                        // Bind das funcoes de controle da janela
                        jan->wv->bind("__clark_minimizar", [hwnd](const std::string&) -> std::string {
                            ShowWindow(hwnd, SW_MINIMIZE);
                            return "";
                        });
                        jan->wv->bind("__clark_maximizar", [hwnd](const std::string&) -> std::string {
                            if (IsZoomed(hwnd)) {
                                ShowWindow(hwnd, SW_RESTORE);
                            } else {
                                ShowWindow(hwnd, SW_MAXIMIZE);
                            }
                            return "";
                        });
                        jan->wv->bind("__clark_fechar", [jan](const std::string&) -> std::string {
                            if (jan->wv) jan->wv->terminate();
                            return "";
                        });
                        jan->wv->bind("__clark_mover", [hwnd](const std::string&) -> std::string {
                            ReleaseCapture();
                            SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                            return "";
                        });
#else
                    // =========================================================
                    // LINUX: Remove decoracao via GTK
                    // =========================================================
                    GtkWidget* window_widget = (GtkWidget*)jan->wv->window().value();
                    GtkWindow* gtk_win = GTK_WINDOW(window_widget);
                    if (gtk_win) {
                        gtk_window_set_decorated(gtk_win, FALSE);

                        jan->sem_borda = true;

                        // Bind das funcoes de controle da janela
                        jan->wv->bind("__clark_minimizar", [gtk_win](const std::string&) -> std::string {
                            gtk_window_iconify(gtk_win);
                            return "";
                        });
                        jan->wv->bind("__clark_maximizar", [gtk_win](const std::string&) -> std::string {
                            if (gtk_window_is_maximized(gtk_win)) {
                                gtk_window_unmaximize(gtk_win);
                            } else {
                                gtk_window_maximize(gtk_win);
                            }
                            return "";
                        });
                        jan->wv->bind("__clark_fechar", [jan](const std::string&) -> std::string {
                            if (jan->wv) jan->wv->terminate();
                            return "";
                        });
                        jan->wv->bind("__clark_mover", [gtk_win](const std::string&) -> std::string {
                            // No Linux, o drag da janela eh feito via JS (mousedown + movimentacao)
                            // Inicia o drag nativo do GTK usando coordenadas do ponteiro
                            GdkWindow* gdk_win = gtk_widget_get_window(GTK_WIDGET(gtk_win));
                            if (gdk_win) {
                                GdkDevice* device = nullptr;
                                GdkSeat* seat = gdk_display_get_default_seat(gdk_display_get_default());
                                if (seat) {
                                    device = gdk_seat_get_pointer(seat);
                                }
                                if (device) {
                                    gint x, y;
                                    gdk_device_get_position(device, NULL, &x, &y);
                                    gtk_window_begin_move_drag(gtk_win, 1, x, y, GDK_CURRENT_TIME);
                                }
                            }
                            return "";
                        });
#endif
                        // Injeta CSS e JS da barra de titulo customizada (cross-platform)
                        jan->wv->eval(R"JS(
                            (function() {
                                // CSS da barra
                                var style = document.createElement('style');
                                style.textContent = `
                                    .__clark_titlebar {
                                        position: fixed;
                                        top: 0; left: 0; right: 0;
                                        height: 32px;
                                        background: rgba(30,30,30,0.95);
                                        display: flex;
                                        align-items: center;
                                        z-index: 999999;
                                        user-select: none;
                                        -webkit-user-select: none;
                                        font-family: 'Segoe UI', Arial, sans-serif;
                                        font-size: 12px;
                                        color: rgba(255,255,255,0.85);
                                    }
                                    .__clark_titlebar_drag {
                                        flex: 1;
                                        height: 100%;
                                        display: flex;
                                        align-items: center;
                                        padding-left: 12px;
                                        cursor: default;
                                    }
                                    .__clark_titlebar_btns {
                                        display: flex;
                                        height: 100%;
                                    }
                                    .__clark_titlebar_btn {
                                        width: 46px;
                                        height: 100%;
                                        border: none;
                                        background: transparent;
                                        color: rgba(255,255,255,0.85);
                                        font-size: 10px;
                                        cursor: pointer;
                                        display: flex;
                                        align-items: center;
                                        justify-content: center;
                                        transition: background 0.15s;
                                    }
                                    .__clark_titlebar_btn:hover {
                                        background: rgba(255,255,255,0.1);
                                    }
                                    .__clark_titlebar_btn.__clark_close:hover {
                                        background: #e81123;
                                        color: white;
                                    }
                                    .__clark_titlebar_btn svg {
                                        width: 10px;
                                        height: 10px;
                                        fill: none;
                                        stroke: currentColor;
                                        stroke-width: 1.2;
                                    }
                                    body {
                                        padding-top: 32px !important;
                                    }
                                `;
                                document.head.appendChild(style);

                                // HTML da barra
                                var bar = document.createElement('div');
                                bar.className = '__clark_titlebar';
                                bar.innerHTML = `
                                    <div class="__clark_titlebar_drag" onmousedown="__clark_mover()">
                                        <span>${document.title || ''}</span>
                                    </div>
                                    <div class="__clark_titlebar_btns">
                                        <button class="__clark_titlebar_btn" onclick="__clark_minimizar()" title="Minimizar">
                                            <svg viewBox="0 0 10 10"><line x1="0" y1="5" x2="10" y2="5"/></svg>
                                        </button>
                                        <button class="__clark_titlebar_btn" onclick="__clark_maximizar()" title="Maximizar">
                                            <svg viewBox="0 0 10 10"><rect x="0.5" y="0.5" width="9" height="9"/></svg>
                                        </button>
                                        <button class="__clark_titlebar_btn __clark_close" onclick="__clark_fechar()" title="Fechar">
                                            <svg viewBox="0 0 10 10"><line x1="0" y1="0" x2="10" y2="10"/><line x1="10" y1="0" x2="0" y2="10"/></svg>
                                        </button>
                                    </div>
                                `;
                                document.body.prepend(bar);
                            })();
                        )JS");
#ifdef _WIN32
                    }
#else
                    }
#endif
                }
                break;
            }
        }
        cmds.pop();
    }
}

// Enfileira um comando e usa dispatch para processá-lo na thread do webview
static void enviar_comando(Janela* jan, Janela::Comando cmd) {
    {
        std::lock_guard<std::mutex> lock(jan->comandos_mutex);
        jan->fila_comandos.push(cmd);
    }
    if (jan->wv) {
        jan->wv->dispatch([jan](void) {
            processar_comandos(jan);
        });
    }
}

// =============================================================================
// FUNÇÕES EXPORTADAS
// =============================================================================

// clark_criar(titulo, largura, altura) -> inteiro (id da janela)
JP_EXPORT int64_t clark_criar(int64_t titulo, int64_t largura, int64_t altura) {
    Janela* jan = new Janela();
    jan->titulo_inicial = std::string((const char*)titulo);
    jan->largura_inicial = (int)largura;
    jan->altura_inicial = (int)altura;

    int id = proximo_id.fetch_add(1);

    {
        std::lock_guard<std::mutex> lock(janelas_mutex);
        janelas[id] = jan;
    }

    jan->wv_thread = std::thread(webview_thread_func, jan);

    // Aguarda o webview ficar pronto
    while (!jan->pronta.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    printf("[Clark] Janela %d criada (%dx%d)\n", id, (int)largura, (int)altura);
    return (int64_t)id;
}

// clark_html(janela, html) -> inteiro
JP_EXPORT int64_t clark_html(int64_t janela_id, int64_t html) {
    std::lock_guard<std::mutex> lock(janelas_mutex);
    auto it = janelas.find((int)janela_id);
    if (it == janelas.end() || !it->second->ativa.load()) return 0;

    Janela::Comando cmd;
    cmd.tipo = Janela::Comando::CMD_HTML;
    cmd.arg1 = std::string((const char*)html);
    enviar_comando(it->second, cmd);
    return 1;
}

// clark_arquivo(janela, caminho) -> inteiro
JP_EXPORT int64_t clark_arquivo(int64_t janela_id, int64_t caminho) {
    std::lock_guard<std::mutex> lock(janelas_mutex);
    auto it = janelas.find((int)janela_id);
    if (it == janelas.end() || !it->second->ativa.load()) return 0;

    Janela::Comando cmd;
    cmd.tipo = Janela::Comando::CMD_ARQUIVO;
    cmd.arg1 = std::string((const char*)caminho);
    enviar_comando(it->second, cmd);
    return 1;
}

// clark_js(janela, codigo) -> inteiro
JP_EXPORT int64_t clark_js(int64_t janela_id, int64_t codigo) {
    std::lock_guard<std::mutex> lock(janelas_mutex);
    auto it = janelas.find((int)janela_id);
    if (it == janelas.end() || !it->second->ativa.load()) return 0;

    Janela::Comando cmd;
    cmd.tipo = Janela::Comando::CMD_JS;
    cmd.arg1 = std::string((const char*)codigo);
    enviar_comando(it->second, cmd);
    return 1;
}

// clark_bind(janela, nome) -> inteiro
JP_EXPORT int64_t clark_bind(int64_t janela_id, int64_t nome) {
    std::lock_guard<std::mutex> lock(janelas_mutex);
    auto it = janelas.find((int)janela_id);
    if (it == janelas.end() || !it->second->ativa.load()) return 0;

    Janela::Comando cmd;
    cmd.tipo = Janela::Comando::CMD_BIND;
    cmd.arg1 = std::string((const char*)nome);
    enviar_comando(it->second, cmd);
    return 1;
}

// clark_ativo(janela) -> inteiro (1 = aberta, 0 = fechada)
JP_EXPORT int64_t clark_ativo(int64_t janela_id) {
    std::lock_guard<std::mutex> lock(janelas_mutex);
    auto it = janelas.find((int)janela_id);
    if (it == janelas.end()) return 0;
    return it->second->ativa.load() ? 1 : 0;
}

// clark_evento(janela) -> texto (nome do evento ou "")
JP_EXPORT int64_t clark_evento(int64_t janela_id) {
    Janela* jan = nullptr;
    {
        std::lock_guard<std::mutex> lock(janelas_mutex);
        auto it = janelas.find((int)janela_id);
        if (it == janelas.end()) return retorna_str("");
        jan = it->second;
    }

    std::lock_guard<std::mutex> lock(jan->eventos_mutex);
    if (jan->fila_eventos.empty()) return retorna_str("");
    Janela::Evento ev = jan->fila_eventos.front();
    jan->fila_eventos.pop();
    jan->ultimo_valor = ev.valor;
    return retorna_str(ev.nome);
}

// clark_valor_js(janela) -> texto (valor do ultimo evento)
JP_EXPORT int64_t clark_valor_js(int64_t janela_id) {
    Janela* jan = nullptr;
    {
        std::lock_guard<std::mutex> lock(janelas_mutex);
        auto it = janelas.find((int)janela_id);
        if (it == janelas.end()) return retorna_str("");
        jan = it->second;
    }

    std::lock_guard<std::mutex> lock(jan->eventos_mutex);
    return retorna_str(jan->ultimo_valor);
}

// clark_titulo(janela, texto) -> inteiro
JP_EXPORT int64_t clark_titulo(int64_t janela_id, int64_t texto) {
    std::lock_guard<std::mutex> lock(janelas_mutex);
    auto it = janelas.find((int)janela_id);
    if (it == janelas.end() || !it->second->ativa.load()) return 0;

    Janela::Comando cmd;
    cmd.tipo = Janela::Comando::CMD_TITULO;
    cmd.arg1 = std::string((const char*)texto);
    enviar_comando(it->second, cmd);
    return 1;
}

// clark_tamanho(janela, largura, altura) -> inteiro
JP_EXPORT int64_t clark_tamanho(int64_t janela_id, int64_t largura, int64_t altura) {
    std::lock_guard<std::mutex> lock(janelas_mutex);
    auto it = janelas.find((int)janela_id);
    if (it == janelas.end() || !it->second->ativa.load()) return 0;

    Janela::Comando cmd;
    cmd.tipo = Janela::Comando::CMD_TAMANHO;
    cmd.largura = (int)largura;
    cmd.altura = (int)altura;
    enviar_comando(it->second, cmd);
    return 1;
}

// clark_sem_borda(janela) -> inteiro
JP_EXPORT int64_t clark_sem_borda(int64_t janela_id) {
    std::lock_guard<std::mutex> lock(janelas_mutex);
    auto it = janelas.find((int)janela_id);
    if (it == janelas.end() || !it->second->ativa.load()) return 0;

    Janela::Comando cmd;
    cmd.tipo = Janela::Comando::CMD_SEM_BORDA;
    enviar_comando(it->second, cmd);
    return 1;
}

// clark_fechar(janela) -> inteiro
JP_EXPORT int64_t clark_fechar(int64_t janela_id) {
    Janela* jan = nullptr;
    {
        std::lock_guard<std::mutex> lock(janelas_mutex);
        auto it = janelas.find((int)janela_id);
        if (it == janelas.end()) return 0;
        jan = it->second;
    }

    Janela::Comando cmd;
    cmd.tipo = Janela::Comando::CMD_FECHAR;
    enviar_comando(jan, cmd);

    // Aguarda a thread encerrar
    if (jan->wv_thread.joinable()) {
        jan->wv_thread.join();
    }

    {
        std::lock_guard<std::mutex> lock(janelas_mutex);
        janelas.erase((int)janela_id);
    }

    delete jan;
    printf("[Clark] Janela %d fechada\n", (int)janela_id);
    return 1;
}