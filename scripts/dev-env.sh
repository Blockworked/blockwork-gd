# Source this file from zsh or bash before using the Geode CLI.
if [ -n "${ZSH_VERSION:-}" ]; then
    _geode_env_file="${(%):-%N}"
else
    _geode_env_file="${BASH_SOURCE[0]}"
fi
_geode_repo="$(cd "$(dirname "$_geode_env_file")/.." && pwd)"
export PATH="$HOME/.local/bin:$PATH"
export GEODE_SDK="$_geode_repo/.tools/geode-sdk"
export GEODE_BINDINGS_REPO_PATH="$_geode_repo/.tools/bindings"
export CPM_SOURCE_CACHE="$_geode_repo/.tools/cpm-cache"
unset _geode_env_file _geode_repo
