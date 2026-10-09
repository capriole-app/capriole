fn main() {
    // The shell's own commands, each allowed by name in capabilities/ (phase 11c).
    tauri_build::try_build(tauri_build::Attributes::new().app_manifest(tauri_build::AppManifest::new().commands(&[
        "pkcs11_tokens",
        "pkcs11_certificates",
        "pkcs11_sign",
        "net_fetch",
    ])))
    .expect("failed to run the Tauri build script");
}
