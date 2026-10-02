//! Capriole desktop (PLAN.md phase 8c, ADR 0008): the web app of apps/capriole in a webview. The engine
//! stays the WebAssembly module in the page's worker; the shell adds the system's file dialogs and
//! reading/writing the files the person picks (plugins dialog and fs, scoped by capabilities/).

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .run(tauri::generate_context!())
        .expect("error while running Capriole");
}
