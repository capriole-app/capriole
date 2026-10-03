// Where files come from and where they go. In the desktop shell (apps/desktop, Tauri): the system's
// dialogs, and the files themselves on disk — "Save" writes the file that was opened; the shell's
// plugins are loaded only there (dynamic imports). In a browser with the File System Access API
// (Chromium): the system's dialogs too, and "Save" writes the file opened or saved before, through its
// handle. In the others: a file input, and downloads (the browser decides where they go).
import { download } from './files';

/** True inside the desktop shell. */
export const isDesktop = typeof window !== 'undefined' && '__TAURI_INTERNALS__' in window;

interface FilePickerOptions {
  suggestedName?: string;
  types?: { description: string; accept: Record<string, string[]> }[];
  excludeAcceptAllOption?: boolean;
}
interface FileAccessWindow {
  showOpenFilePicker(options?: FilePickerOptions & { multiple?: boolean }): Promise<FileSystemFileHandle[]>;
  showSaveFilePicker(options?: FilePickerOptions): Promise<FileSystemFileHandle>;
}

function fileAccess(): FileAccessWindow | null {
  if (isDesktop || typeof window === 'undefined') return null;
  return 'showOpenFilePicker' in window && 'showSaveFilePicker' in window
    ? (window as unknown as FileAccessWindow)
    : null;
}

/** True where the person chooses where files are opened from and saved to (not only downloads). */
export const choosesWhere = isDesktop || fileAccess() !== null;

const pdfTypes = [{ description: 'PDF', accept: { 'application/pdf': ['.pdf'] } }];

/** The person closed the dialog. */
function cancelled(error: unknown): boolean {
  return error instanceof DOMException && error.name === 'AbortError';
}

/** A PDF the person picked: its name, its bytes, and where it lives (desktop: a path; browser: a handle). */
export interface PickedFile {
  name: string;
  data: ArrayBuffer;
  path: string | null;
  handle?: FileSystemFileHandle | null;
}

/** Where a document is saved: its path (desktop), or its handle (browser); null: ask. */
export interface SaveTarget {
  path: string | null;
  handle: FileSystemFileHandle | null;
}

const pdfFilter = [{ name: 'PDF', extensions: ['pdf'] }];

function fileName(path: string): string {
  return path.split(/[\\/]/).pop() ?? path;
}

/** A file from the browser's file input (or a drop). */
export async function fromBrowser(file: File): Promise<PickedFile> {
  return { name: file.name, data: await file.arrayBuffer(), path: null };
}

/** A PDF chosen in the system's open dialog (desktop, or a browser with file access); null when cancelled. */
export async function pickPdf(): Promise<PickedFile | null> {
  const access = fileAccess();
  if (access) {
    try {
      const [handle] = await access.showOpenFilePicker({ types: pdfTypes, multiple: false });
      if (!handle) return null;
      const file = await handle.getFile();
      return { name: file.name, data: await file.arrayBuffer(), path: null, handle };
    } catch (error) {
      if (cancelled(error)) return null;
      throw error;
    }
  }
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
 * Writes a PDF made by `make`. Desktop: to the target's path, otherwise where the save dialog says. A
 * browser with file access: through the target's handle (the browser asks once for permission to write
 * it), otherwise where the save dialog says. Other browsers: a download named `name`. The dialog comes
 * before the bytes are made: the browser shows it only right after the click. null when the person
 * cancels; otherwise where it went (path and handle null for a download) and its name.
 */
export async function savePdf(
  make: () => Promise<ArrayBuffer>,
  name: string,
  target: SaveTarget | null,
): Promise<(SaveTarget & { name: string }) | null> {
  const access = fileAccess();
  if (access) {
    let handle = target?.handle ?? null;
    try {
      handle ??= await access.showSaveFilePicker({ suggestedName: name, types: pdfTypes });
      const writable = await handle.createWritable();
      try {
        await writable.write(await make());
      } finally {
        await writable.close();
      }
    } catch (error) {
      if (cancelled(error)) return null;
      throw error;
    }
    return { path: null, handle, name: handle.name };
  }
  if (!isDesktop) {
    download(await make(), name);
    return { path: null, handle: null, name };
  }
  let path = target?.path ?? null;
  if (path === null) {
    const { save } = await import('@tauri-apps/plugin-dialog');
    path = await save({ defaultPath: name, filters: pdfFilter });
    if (path === null) return null;
  }
  const { writeFile } = await import('@tauri-apps/plugin-fs');
  await writeFile(path, new Uint8Array(await make()));
  return { path, handle: null, name: fileName(path) };
}

export { fileName };
