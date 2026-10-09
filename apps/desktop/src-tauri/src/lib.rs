//! Capriole desktop (PLAN.md phase 8c, ADR 0008): the web app of apps/capriole in a webview. The engine
//! stays the WebAssembly module in the page's worker; the shell adds the system's file dialogs and
//! reading/writing the files the person picks (plugins dialog and fs, scoped by capabilities/), and
//! what a page cannot do (phase 11c): smart cards through PKCS#11 (pkcs11.rs) and the network calls a
//! long-term signature needs (net.rs). Card and network calls run off the main thread.

mod net;
mod pkcs11;

use cryptoki::types::AuthPin;

async fn blocking<T: Send + 'static>(f: impl FnOnce() -> Result<T, String> + Send + 'static) -> Result<T, String> {
    tauri::async_runtime::spawn_blocking(f).await.map_err(|e| format!("internal: {e}"))?
}

/// The cards in the readers the module sees.
#[tauri::command]
async fn pkcs11_tokens(module: String) -> Result<Vec<pkcs11::Token>, String> {
    blocking(move || pkcs11::tokens(&module)).await
}

/// The certificates of the card in slot.
#[tauri::command]
async fn pkcs11_certificates(module: String, slot: u64) -> Result<Vec<pkcs11::Certificate>, String> {
    blocking(move || pkcs11::certificates(&module, slot)).await
}

/// The card signs what the engine prepared, with the key of the certificate key_id, after the PIN.
#[tauri::command]
async fn pkcs11_sign(module: String, slot: u64, key_id: String, pin: String, to_sign: pkcs11::ToSign) -> Result<String, String> {
    let pin = AuthPin::from(pin);
    blocking(move || pkcs11::sign(&module, slot, &key_id, pin, &to_sign)).await
}

/// One request of a long-term signature (a time stamp, an OCSP answer, a CRL, an issuer's
/// certificate): body and answer in hexadecimal.
#[tauri::command]
async fn net_fetch(purpose: net::Purpose, url: String, body: String) -> Result<String, String> {
    blocking(move || net::fetch(purpose, &url, &pkcs11::unhex(&body)?).map(|b| pkcs11::hex(&b))).await
}

/// The shell: its plugins and its own commands.
fn shell<R: tauri::Runtime>(builder: tauri::Builder<R>) -> tauri::Builder<R> {
    builder
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .invoke_handler(tauri::generate_handler![pkcs11_tokens, pkcs11_certificates, pkcs11_sign, net_fetch])
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    shell(tauri::Builder::default()).run(tauri::generate_context!()).expect("error while running Capriole");
}

#[cfg(test)]
mod tests {
    //! The commands as the page calls them (apps/capriole/src/lib/smartcard.ts: its argument names,
    //! camelCase), through the IPC with the app's own capabilities: what reaches the shell, not an
    //! argument or permission error.
    use serde_json::json;
    use tauri::ipc::{CallbackFn, InvokeBody};
    use tauri::test::{get_ipc_response, mock_builder, INVOKE_KEY};
    use tauri::webview::InvokeRequest;

    fn call(webview: &tauri::WebviewWindow<tauri::test::MockRuntime>, cmd: &str, args: serde_json::Value) -> Result<serde_json::Value, serde_json::Value> {
        get_ipc_response(
            webview,
            InvokeRequest {
                cmd: cmd.into(),
                callback: CallbackFn(0),
                error: CallbackFn(1),
                url: "tauri://localhost".parse().unwrap(),
                body: InvokeBody::Json(args),
                headers: Default::default(),
                invoke_key: INVOKE_KEY.to_string(),
            },
        )
        .map(|b| b.deserialize::<serde_json::Value>().unwrap())
    }

    #[test]
    fn the_pages_calls_reach_the_shell() {
        let app = super::shell(mock_builder()).build(tauri::generate_context!()).unwrap();
        let webview = tauri::WebviewWindowBuilder::new(&app, "main", Default::default()).build().unwrap();
        let module = "/nonexistent/module.so";
        let to_sign = json!({"hash": "00", "digestInfo": "00", "data": "00", "key": "RSA 2048"});
        for (cmd, args) in [
            ("pkcs11_tokens", json!({"module": module})),
            ("pkcs11_certificates", json!({"module": module, "slot": 1})),
            ("pkcs11_sign", json!({"module": module, "slot": 1, "keyId": "01", "pin": "1234", "toSign": to_sign})),
        ] {
            let error = call(&webview, cmd, args).unwrap_err();
            assert!(error.as_str().unwrap_or("").starts_with("module:"), "{cmd}: {error}");
        }
        let error = call(&webview, "net_fetch", json!({"purpose": "ocsp", "url": "file:///etc/passwd", "body": "00"})).unwrap_err();
        assert!(error.as_str().unwrap_or("").starts_with("url:"), "{error}");
        // Not one of the shell's commands: refused.
        assert!(call(&webview, "net_anything", json!({})).is_err());
    }
}
