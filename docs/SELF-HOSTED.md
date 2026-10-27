# Self-hosting Capriole's web app

Capriole's web app is static files only. The PDF engine runs in the browser as WebAssembly, so your server
never receives a document: it serves the page, the engine and its resources, nothing else. You can run the
same build as [capriole.app](https://capriole.app) on your own domain, inside your network, or offline.

Every release (`github.com/capriole-app/capriole/releases`) offers it in two forms:

| Form | What it is |
|---|---|
| The image `ghcr.io/capriole-app/capriole-web:<version>` | Caddy serving the app on port 8080, with the headers it needs. The simplest way |
| `capriole-web.tar.gz` (a file of the release, listed in its `SHA256SUMS`) | The same files for any web server, with the image's own Caddyfile and policy |

## With the image

[`deploy/self-hosted/docker-compose.yml`](../deploy/self-hosted/docker-compose.yml) runs one container that is
read-only, has no Linux capabilities and can regain no privileges:

```sh
curl -fsSLO https://raw.githubusercontent.com/capriole-app/capriole/main/deploy/self-hosted/docker-compose.yml
CAPRIOLE_VERSION=0.4.3 docker compose up -d
curl -sI http://localhost:8080/ | grep -i content-security-policy   # the app's policy is there
```

The container speaks plain HTTP on `127.0.0.1:8080`. Put it behind the reverse proxy that terminates TLS for
your domain (Caddy, nginx, Traefik…). Browsers give WebAssembly workers and file pickers only to secure
pages, so serve the app over HTTPS, or open it on `localhost`. To update, set the new version and run
`docker compose up -d` again.

## With another web server

```sh
sha256sum -c --ignore-missing SHA256SUMS
tar -xzf capriole-web.tar.gz          # context/: build/ (the site), Caddyfile, csp.caddy, Dockerfile
```

Serve `context/build/` as the site's root. The app works only with the headers the image sends
(`context/Caddyfile`):

- **`Content-Security-Policy`**: the value is in `context/csp.caddy`. It carries the hashes of this build's
  inline scripts, so take it from the same archive at every update. Without it the app still works, but
  without the protection.
- `Cross-Origin-Opener-Policy: same-origin`, `X-Content-Type-Options: nosniff`, `Referrer-Policy: no-referrer`,
  `X-Frame-Options: DENY`.
- `.wasm` files served as `application/wasm` (most servers do this already).
- Caching: `/_app/immutable/*` may be cached for a year, everything else `no-cache` (the page, the
  resource pack, the licences).

## What differs from capriole.app

- **«Report a problem»** sends to `/api/report` on the same server. Only capriole.app has a service behind it,
  so on your server the report fails and the app suggests writing to the project instead. Your users' reports
  never reach the project or a third party.
- **The visit count**: the image writes one anonymous line per opening of the page to `/logs` (time, status,
  browser name; no address, no path, no other header). The compose above keeps it in memory only, so it is
  gone at each restart.
- No account, no tracking, no request to any other site: the app's policy allows none.

The app is under the 0BSD licence (`LICENSE`). The notices of what it is built from are in the app's «About
and licences» and in `licenses.txt` at the site's root.
