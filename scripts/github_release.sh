#!/usr/bin/env bash
# A GitHub release of capriole-app/capriole for a tag, with every file of a directory as its assets
# (Jenkinsfile, stage Desktop: the files of dist/release and their SHA256SUMS).
#
#   GITHUB_TOKEN=… scripts/github_release.sh v0.1.0 dist/release
#
# The token: fine-grained, owner capriole-app, this repository only, Contents read and write. The release
# is made as a draft, the files are uploaded, and only then is it published: the «latest» links never
# point at a release with some of its files missing. A tag with a hyphen (v0.1.0-rc1) makes a pre-release,
# which «latest» skips. A tag already released fails: a release is not overwritten.
set -euo pipefail

tag="${1:?usage: github_release.sh <tag> <directory>}"
dir="${2:?usage: github_release.sh <tag> <directory>}"
: "${GITHUB_TOKEN:?GITHUB_TOKEN is not set}"
repo=capriole-app/capriole
api="https://api.github.com/repos/$repo"
[[ "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$ ]] || { echo "not a release tag: $tag" >&2; exit 1; }
compgen -G "$dir/*" >/dev/null || { echo "nothing to release in $dir" >&2; exit 1; }

github() {  # method url [curl args…]: the answer's body on stdout, fails on an HTTP error
  local method=$1 url=$2
  shift 2
  curl -fsS --retry 3 -X "$method" "$url" \
    -H "Authorization: Bearer $GITHUB_TOKEN" -H "Accept: application/vnd.github+json" \
    -H "X-GitHub-Api-Version: 2022-11-28" "$@"
}
field() { python3 -c "import json, sys; print(json.load(sys.stdin)[sys.argv[1]])" "$1"; }

status="$(curl -sS -o /dev/null -w '%{http_code}' -H "Authorization: Bearer $GITHUB_TOKEN" "$api/releases/tags/$tag")"
[[ "$status" == 404 ]] || { echo "$tag: a release exists already (or GitHub answered $status)" >&2; exit 1; }

prerelease=false
[[ "$tag" == *-* ]] && prerelease=true
body="$(python3 -c 'import json, sys; print(json.dumps({"tag_name": sys.argv[1], "name": "Capriole " + sys.argv[1][1:],
  "draft": True, "prerelease": sys.argv[2] == "true", "generate_release_notes": True}))' "$tag" "$prerelease")"
release="$(github POST "$api/releases" -d "$body")"
id="$(field id <<<"$release")"
echo "draft release $id for $tag (prerelease: $prerelease)"

for file in "$dir"/*; do
  name="$(basename "$file")"
  github POST "https://uploads.github.com/repos/$repo/releases/$id/assets?name=$name" \
    -H "Content-Type: application/octet-stream" --data-binary "@$file" >/dev/null
  echo "  uploaded $name ($(du -h "$file" | cut -f1))"
done

url="$(github PATCH "$api/releases/$id" -d '{"draft": false}' | field html_url)"
echo "published: $url"
