// The engine for an app: a Web Worker running the WebAssembly module (bindings/wasm) through the C API,
// and the JSON it returns (capi/README.md). Shared by the apps (ADR 0008): apps/inspector, apps/capriole.
export { EngineClient, EngineError } from './client';
export * from './types';
