// Where the components find the editor. The page is rendered at build time too (the landing, for search
// engines), where there is no editor: the engine runs in a worker and the stores in IndexedDB, browser
// things. So the editor is made in the browser once the page is up, and the first screen (the landing and
// its card) asks for it here: undefined until then. Only types come from editor.svelte.ts, so that the
// landing's modules never load it.
import { createContext } from 'svelte';
import type { Editor } from './editor.svelte';

/** The editor, for everything that is shown once it exists (the pages, the dialogs). */
export const [getEditor, setEditor] = createContext<Editor>();

/** What the first screen knows of the app. */
export interface AppState {
  /** The editor, once the app has loaded in the browser. */
  readonly editor: Editor | undefined;
  /** The app's code could not be loaded (a new version deployed meanwhile, the network gone). */
  readonly failed: boolean;
}

export const [getApp, setApp] = createContext<AppState>();
