// JPLangActivity.java
// Activity minima do Android para JPLang — WebView fullscreen com ponte nativa
//
// Esta Activity:
//   1. Cria uma WebView fullscreen
//   2. Carrega a .so nativa do usuario (libjplang_app.so)
//   3. A .so pode enviar HTML/JS pra WebView via JNI
//   4. A WebView pode chamar a .so via JavascriptInterface
//
// O usuario nunca edita este arquivo — ele eh gerado automaticamente pelo jp_android.

package com.jplang.app;

import android.app.Activity;
import android.os.Bundle;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.webkit.WebSettings;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.view.Window;
import android.view.WindowManager;

public class JPLangActivity extends Activity {

    private WebView webView;

    // Carregar biblioteca nativa
    static {
        System.loadLibrary("jplang_app");
    }

    // Funcoes nativas implementadas em C++ (na .so do usuario)
    private static native String nativeGetHtml();
    private static native String nativeOnEvent(String event, String data);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Fullscreen
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(
            WindowManager.LayoutParams.FLAG_FULLSCREEN,
            WindowManager.LayoutParams.FLAG_FULLSCREEN
        );

        // Criar WebView
        webView = new WebView(this);
        setContentView(webView);

        // Configurar WebView
        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setAllowFileAccess(true);

        webView.setWebViewClient(new WebViewClient());
        webView.setWebChromeClient(new WebChromeClient());

        // Ponte Java -> nativo
        webView.addJavascriptInterface(new JpBridge(), "jp");

        // Carregar HTML da funcao nativa
        String html = nativeGetHtml();
        if (html != null && !html.isEmpty()) {
            webView.loadDataWithBaseURL("file:///", html, "text/html", "UTF-8", null);
        } else {
            webView.loadDataWithBaseURL("file:///",
                "<html><body style='background:#111;color:#fff;display:flex;align-items:center;justify-content:center;height:100vh;margin:0;font-family:sans-serif'>" +
                "<h1>JPLang Android</h1></body></html>",
                "text/html", "UTF-8", null);
        }
    }

    // Ponte JavaScript -> nativo
    private class JpBridge {
        @JavascriptInterface
        public String evento(String event, String data) {
            return nativeOnEvent(event, data);
        }

        @JavascriptInterface
        public void log(String msg) {
            android.util.Log.d("JPLang", msg);
        }
    }

    // Executar JS na WebView (chamado do nativo via JNI)
    public void runJs(final String js) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                webView.evaluateJavascript(js, null);
            }
        });
    }

    @Override
    public void onBackPressed() {
        if (webView.canGoBack()) {
            webView.goBack();
        } else {
            super.onBackPressed();
        }
    }
}