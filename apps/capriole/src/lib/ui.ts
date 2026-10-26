// The interface's shared looks (Tailwind classes), so that every bar, menu and dialog matches: flat, hairlines,
// one filled button per place (the main action). The colours are the tokens of app.css.

/** A button of a bar: no border, an icon (its name in aria-label and title) and, when there is room, its text. */
export const barButton =
  'flex h-9 min-w-9 shrink-0 items-center justify-center gap-1.5 rounded-md px-2 text-sm hover:bg-muted disabled:pointer-events-none disabled:opacity-40';

/** A tool of the page's bar: a bar's button with only its icon, narrower (fifteen of them stand in a row). */
export const toolButton =
  'flex h-9 w-8 shrink-0 items-center justify-center rounded-md hover:bg-muted disabled:pointer-events-none disabled:opacity-40';

/** A bar's button that is pressed: its panel open, its tool chosen. */
export const pressed = 'bg-accent text-emphasis hover:bg-accent';

/** The main action: the one filled button of a bar or a dialog. */
export const primaryButton =
  'flex h-9 shrink-0 items-center justify-center gap-2 rounded-md bg-primary px-4 text-sm font-medium text-primary-foreground hover:bg-primary/90 disabled:pointer-events-none disabled:opacity-60';

/** A button with its text beside the main one (Cancel, Choose…): a hairline border. */
export const secondaryButton =
  'flex h-9 shrink-0 items-center justify-center gap-2 rounded-md border bg-card px-4 text-sm hover:bg-muted disabled:pointer-events-none disabled:opacity-50';

/**
 * On a phone (below md): the commands of a bar along the bottom of the screen, in reach of the thumb, on one
 * line (EditorApp leaves its height free under the page). From md up, `contents`: the commands stay in their bar.
 */
export const bottomBar =
  'contents max-md:fixed max-md:inset-x-0 max-md:bottom-0 max-md:z-10 max-md:flex max-md:h-14 max-md:items-center max-md:gap-1 max-md:border-t max-md:bg-background max-md:px-2';

/** The part of the bottom bar that scrolls sideways when it does not fit (nothing in it opens a menu). */
export const bottomScroll = 'max-md:flex max-md:min-w-0 max-md:flex-1 max-md:flex-nowrap max-md:overflow-x-auto';

/** The thin line between a bar's groups. */
export const separator = 'mx-1 h-5 w-px shrink-0 bg-border';

/** A menu that opens under its button, and its items. */
export const menu = 'absolute end-0 z-20 mt-1 min-w-44 rounded-lg border bg-card p-1 shadow-lg';
export const menuItem = 'flex w-full items-center gap-2 rounded-md px-2 py-1.5 text-start text-sm hover:bg-muted';
export const menuLabel = 'px-2 pt-2 pb-1 text-xs font-medium text-muted-foreground';

/** A dialog (with its max-w-…): a card over the page, the page dimmed behind it. */
export const dialog = 'm-auto w-full rounded-xl border bg-card p-6 text-foreground shadow-xl backdrop:bg-black/30';
export const dialogTitle = 'flex items-center gap-2 text-lg font-semibold tracking-tight';
/** The dialog's buttons, under a hairline across it. */
export const dialogFooter = '-mx-6 flex justify-end gap-2 border-t px-6 pt-4';

/** A field (input, select): the blue line and a soft ring when it has the focus. */
export const field =
  'rounded-md border bg-card px-3 py-2 text-base focus:border-emphasis sm:text-sm focus:ring-3 focus:ring-accent focus:outline-none';
