#!/usr/bin/env bash
# Run docker (or any command that needs the docker socket). If this shell was
# started before `usermod -aG docker`, re-exec via `sg docker`.
#
# sg -c is evaluated by /bin/sh (dash). Do not use bash `printf %q` ($'…');
# that is what turned cmake/ninja recipes into "\ntcmake: command not found".
set -euo pipefail

posix_quote() {
  local s=$1
  s=${s//\'/\'\\\'\'}
  printf "'%s'" "$s"
}

posix_join() {
  local out="" a
  for a in "$@"; do
    out+=" $(posix_quote "$a")"
  done
  printf '%s' "${out# }"
}

if id -nG 2>/dev/null | grep -qw docker; then
  exec "$@"
fi

if getent group docker 2>/dev/null | grep -qE "(^|:|,)${USER}(,|$)"; then
  exec sg docker -c "$(posix_join "$@")"
fi

exec "$@"
