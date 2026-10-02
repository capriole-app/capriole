// Where files come from and where they go. In the browser: a file input, and downloads. In the desktop
// shell (apps/desktop, Tauri): the system's dialogs, and the files themselves on disk — "Save" writes
// the file that was opened. The shell's plugins are loaded only there (dynamic imports).
import { download } from './files';

/** True inside the desktop shell. */
export const isDesktop = typeof window !== 'undefined' && '__TAURI_INTERNALS__' in window;

/** A PDF the person picked: its name, its bytes, and on the desktop where it lives. */
export interface PickedFile {
  name: string;
  data: ArrayBuffer;
  path: string | null;
}

const pdfFilter = [{ name: 'PDF', extensions: ['pdf'] }];

function fileName(path: string): string {
  return path.split(/[\\/]/).pop() ?? path;
}

/** A file from the browser's file input (or a drop). */
export async function fromBrowser(file: File): Promise<PickedFile> {
  return { name: file.name, data: await file.arrayBuffer(), path: null };
}

/** Desktop: a PDF chosen in the system's open dialog; null when the person cancels. */
export async function pickPdf(): Promise<PickedFile | null> {
  const { open } = await import('@tauri-apps/plugin-dialog');
  const { readFile } = await import('@tauri-apps/plugin-fs');
  const path = await open({ multiple: false, directory: false, filters: pdfFilter });
  if (typeof path !== 'string') return null;
  const bytes = await readFile(path);
  return {
    name: fileName(path),
    data: bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) as ArrayBuffer,
    path,
  };
}

/**
 * Writes a PDF. Browser: a download named `name`. Desktop: to `path` when given, otherwise where the
 * system's save dialog says. null when the person cancels; otherwise where it went (path: null for a
 * download).
 */
export async function savePdf(
  data: ArrayBuffer,
  name: string,
  path: string | null,
): Promise<{ path: string | null } | null> {
  if (!isDesktop) {
    download(data, name);
    return { path: null };
  }
  let target = path;
  if (target === null) {
    const { save } = await import('@tauri-apps/plugin-dialog');
    target = await save({ defaultPath: name, filters: pdfFilter });
    if (target === null) return null;
  }
  const { writeFile } = await import('@tauri-apps/plugin-fs');
  await writeFile(target, new Uint8Array(data));
  return { path: target };
}

export { fileName };
