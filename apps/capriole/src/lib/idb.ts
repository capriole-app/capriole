// A small store of records on this device, in the browser's IndexedDB (the desktop shell's web view keeps
// it in the app's data): one database with one object store, records keyed by their `id`. Where there is
// no IndexedDB (a private window may refuse it) nothing is kept: loads give nothing, writes say false.

function request<T>(r: IDBRequest<T>): Promise<T> {
  return new Promise((resolve, reject) => {
    r.onsuccess = () => resolve(r.result);
    r.onerror = () => reject(r.error ?? new Error('IndexedDB'));
  });
}

/**
 * A write done: its transaction committed (a request's success comes before, and a page closed or reloaded
 * in between loses the write).
 */
function committed(s: IDBObjectStore): Promise<void> {
  return new Promise((resolve, reject) => {
    s.transaction.oncomplete = () => resolve();
    s.transaction.onerror = () => reject(s.transaction.error ?? new Error('IndexedDB'));
    s.transaction.onabort = () => reject(s.transaction.error ?? new Error('IndexedDB'));
  });
}

export class RecordStore<T extends { id: string }> {
  #database: Promise<IDBDatabase | null> | null = null;

  constructor(
    readonly databaseName: string,
    readonly storeName: string,
  ) {}

  /** The database, or null where there is none. */
  #open(): Promise<IDBDatabase | null> {
    this.#database ??= new Promise<IDBDatabase | null>((resolve) => {
      try {
        const r = indexedDB.open(this.databaseName, 1);
        r.onupgradeneeded = () => r.result.createObjectStore(this.storeName, { keyPath: 'id' });
        r.onsuccess = () => resolve(r.result);
        r.onerror = () => resolve(null);
        r.onblocked = () => resolve(null);
      } catch {
        resolve(null);
      }
    });
    return this.#database;
  }

  async #store(mode: IDBTransactionMode): Promise<IDBObjectStore | null> {
    const db = await this.#open();
    return db ? db.transaction(this.storeName, mode).objectStore(this.storeName) : null;
  }

  /** Every record kept. */
  async all(): Promise<T[]> {
    try {
      const s = await this.#store('readonly');
      return s ? await request(s.getAll() as IDBRequest<T[]>) : [];
    } catch {
      return [];
    }
  }

  /** Keeps a record (replacing one of the same id); false when it could not be kept. */
  async put(record: T): Promise<boolean> {
    try {
      const s = await this.#store('readwrite');
      if (!s) return false;
      const done = committed(s);
      await request(s.put(record));
      await done;
      return true;
    } catch {
      return false;
    }
  }

  /** Forgets a record. */
  async delete(id: string): Promise<void> {
    try {
      const s = await this.#store('readwrite');
      if (s) {
        const done = committed(s);
        await request(s.delete(id));
        await done;
      }
    } catch {
      // nothing kept to forget
    }
  }
}
