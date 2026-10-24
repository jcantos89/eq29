#!/usr/bin/env bash
# Non-root Debian 13 setup for the managed cloud image. APT validates repository
# signatures and package hashes; no verification bypasses or system writes.
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tools_dir=${EQ29_TOOLS_DIR:-/workspace/eq29-tools}
if [[ $(. /etc/os-release; echo "$ID:$VERSION_ID") != debian:13 ]]; then
    echo 'This helper targets Debian 13; use the native dependencies listed in README.md.' >&2
    exit 1
fi
mkdir -p "$tools_dir/apt/lists/partial" "$tools_dir/apt/cache/archives/partial" "$tools_dir/debs" "$tools_dir/sysroot"
cat > "$tools_dir/apt/sources.list" <<'SOURCES'
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://security.debian.org/debian-security trixie-security main
SOURCES
apt_opts=(-o "Dir::Etc::sourcelist=$tools_dir/apt/sources.list" -o Dir::Etc::sourceparts=-
          -o "Dir::State::lists=$tools_dir/apt/lists" -o "Dir::Cache=$tools_dir/apt/cache")
apt-get "${apt_opts[@]}" update
(
    cd "$tools_dir/debs"
    apt-get "${apt_opts[@]}" download cmake cmake-data libx11-dev libgl-dev libglx-dev \
      libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxrender-dev x11proto-dev \
      xtrans-dev libxau-dev libxdmcp-dev libxcb1-dev libopengl-dev libglu1-mesa-dev \
      liblilv-dev lilv-utils libserd-dev libsord-dev libsratom-dev lv2-dev libzix-dev \
      xvfb xserver-common libxfont2 xauth libjsoncpp26 librhash1 libuv1t64 \
      libxcb1 libx11-6 libxext6 libxrandr2 libxcursor1 libxfixes3 libxrender1 \
      libgl1 libglx0 libopengl0 libglu1-mesa libxau6 libxdmcp6 \
      liblilv-0-0 libserd-0-0 libsord-0-0 libsratom-0-0 libzix-0-0
    for deb in ./*.deb; do dpkg-deb -x "$deb" "$tools_dir/sysroot"; done
)
# A fixed local prefix is retained in the cloud snapshot; no credentials in here.
printf 'export EQ29_SYSROOT=%q\n' "$tools_dir/sysroot" > "$tools_dir/activate.sh"
cat >> "$tools_dir/activate.sh" <<'ACTIVATE'
export PATH="$EQ29_SYSROOT/usr/bin:$PATH"
export LD_LIBRARY_PATH="$EQ29_SYSROOT/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export CMAKE_PREFIX_PATH="$EQ29_SYSROOT/usr${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
export PKG_CONFIG_PATH="$EQ29_SYSROOT/usr/lib/x86_64-linux-gnu/pkgconfig:$EQ29_SYSROOT/usr/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export PKG_CONFIG_SYSROOT_DIR="$EQ29_SYSROOT"
export CPATH="$EQ29_SYSROOT/usr/include${CPATH:+:$CPATH}"
export LIBRARY_PATH="$EQ29_SYSROOT/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}"
export XDG_CACHE_HOME=/workspace/eq29-tools/cache
mkdir -p "$XDG_CACHE_HOME"
ACTIVATE
. "$tools_dir/activate.sh"
cd "$repo_dir"
git submodule update --init --recursive
make
make test
xvfb-run -a -s '-screen 0 1280x900x24 +extension GLX' \
    ./build/eq29_lv2_tests "$repo_dir/build/bin/my_dynamic_eq.lv2/" --ui
