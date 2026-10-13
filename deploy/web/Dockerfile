# capriole-web: the web app (apps/capriole, static files) served by Caddy. The context is the one
# package.sh prepares: build/ (the app, built by the gate) and csp.caddy (its policy). The app needs no
# server: Caddy only serves files, with their headers (Caddyfile).
FROM caddy:2.11@sha256:f2a1290d0463aad60660d4ec134943f183ee2a5f6c3eb7bf32dd984f2f020772

# Without its file capability (bind below 1024: not needed, the port is 8080), the binary runs with
# every capability dropped and no-new-privileges (docker-compose.yml): a binary with file
# capabilities cannot be executed there. busybox cp does not copy them.
RUN cp /usr/bin/caddy /usr/bin/caddy.new && mv /usr/bin/caddy.new /usr/bin/caddy

# /logs: the anonymous count of visits (Caddyfile), a volume on the server; owned by the user Caddy runs
# as, which a new volume takes from here.
RUN mkdir /logs && chown 65532:65532 /logs

COPY Caddyfile /etc/caddy/Caddyfile
COPY csp.caddy /etc/caddy/csp.caddy
COPY build/ /srv/

USER 65532:65532
EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD wget -qO- http://127.0.0.1:8080/healthz >/dev/null || exit 1
CMD ["caddy", "run", "--config", "/etc/caddy/Caddyfile", "--adapter", "caddyfile"]
