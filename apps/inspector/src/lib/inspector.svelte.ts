// The inspector's state: the open file, what the engine says about it, and where the user is looking.
import { createContext } from 'svelte';
import { EngineClient, EngineError } from './engine/client';
import { Status, type ObjectInfo, type Summary } from './engine/types';

export type Tab = 'overview' | 'pages' | 'objects';

export class Inspector {
  readonly engine = new EngineClient();

  engineVersion = $state<string | null>(null);
  file = $state.raw<File | null>(null);
  summary = $state.raw<Summary | null>(null);
  /** Repairs off: strict reading only, to see what the file is without the engine's help. */
  strict = $state(false);
  busy = $state(false);
  error = $state<string | null>(null);
  /** Set when the open file needs a password; the dialog asks for it. */
  passwordNeeded = $state(false);
  passwordRejected = $state(false);

  tab = $state<Tab>('overview');
  /** The object on screen, and the ones visited before it (for "back"). */
  current = $state.raw<ObjectInfo | null>(null);
  history = $state.raw<number[]>([]);

  #password = '';

  constructor() {
    this.engine
      .version()
      .then((version) => (this.engineVersion = version))
      .catch(() => {});
  }

  async open(file: File, password = ''): Promise<void> {
    this.file = file;
    this.#password = password;
    await this.#load();
  }

  async submitPassword(password: string): Promise<void> {
    this.#password = password;
    await this.#load();
  }

  cancelPassword(): void {
    this.passwordNeeded = false;
    this.passwordRejected = false;
    this.file = null;
  }

  async setStrict(strict: boolean): Promise<void> {
    this.strict = strict;
    if (this.file) await this.#load();
  }

  async #load(): Promise<void> {
    if (!this.file) return;
    this.busy = true;
    this.error = null;
    try {
      // The bytes go to the worker (transferred): read them again on every attempt.
      const data = await this.file.arrayBuffer();
      this.summary = await this.engine.open(data, this.#password, !this.strict);
      this.passwordNeeded = false;
      this.passwordRejected = false;
      this.current = null;
      this.history = [];
      this.tab = 'overview';
    } catch (error) {
      if (error instanceof EngineError && error.status === Status.passwordRequired) {
        this.passwordRejected = this.passwordNeeded && this.#password !== '';
        this.passwordNeeded = true;
      } else {
        this.summary = null;
        this.error = describe(error);
      }
    } finally {
      this.busy = false;
    }
  }

  /** Shows an object; with remember, the current one goes on the "back" stack. */
  async show(number: number, remember = true): Promise<void> {
    try {
      const info = await this.engine.object(number);
      if (remember && this.current && this.current.number !== number) {
        this.history = [...this.history, this.current.number];
      }
      this.current = info;
      this.tab = this.tab === 'overview' ? 'objects' : this.tab;
      this.error = null;
    } catch (error) {
      this.error = describe(error);
    }
  }

  async back(): Promise<void> {
    const previous = this.history.at(-1);
    if (previous === undefined) return;
    this.history = this.history.slice(0, -1);
    await this.show(previous, false);
  }

  /** Saves and hands the file to the browser as a download. */
  async save(incremental: boolean, modificationDate: string | null): Promise<void> {
    if (!this.file) return;
    this.busy = true;
    try {
      const data = await this.engine.save(incremental, modificationDate);
      const base = this.file.name.replace(/\.pdf$/i, '');
      download(
        new Blob([data], { type: 'application/pdf' }),
        `${base}-${incremental ? 'incrementale' : 'completo'}.pdf`,
      );
      this.error = null;
    } catch (error) {
      this.error = describe(error);
    } finally {
      this.busy = false;
    }
  }

  async close(): Promise<void> {
    await this.engine.close().catch(() => {});
    this.file = null;
    this.summary = null;
    this.current = null;
    this.history = [];
    this.error = null;
  }
}

export function download(blob: Blob, name: string): void {
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function describe(error: unknown): string {
  if (error instanceof EngineError) return error.message;
  return error instanceof Error ? error.message : String(error);
}

export const [getInspector, setInspector] = createContext<Inspector>();
