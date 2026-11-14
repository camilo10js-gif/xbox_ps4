#!/usr/bin/env bash
set -euo pipefail
root=${1:?Usage: setup.sh toolchain-directory}
mkdir -p "$root"
root=$(cd "$root" && pwd)
curl --fail --location --retry 2 https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.4/toolchain-llvm-18.tar.gz -o "$root/oo.tar.gz"
printf '%s  %s\n' 3c7cd5bb593ca74fa1c13fd59f3938dc0fc07985167f7275063019e63abe4526 "$root/oo.tar.gz" | sha256sum -c -
mkdir -p "$root/oo"
tar -xf "$root/oo.tar.gz" -C "$root/oo"
if [[ ! -d "$root/sdk/.git" ]]; then git clone https://github.com/GoldHEN/GoldHEN_Plugins_SDK "$root/sdk"; fi
git -C "$root/sdk" checkout bcea3c7ef01dac6d7f9f49ebf9e90fe66d86f5f7
curl --fail --location --retry 2 'https://deb.debian.org/debian/pool/main/o/openssl/libssl1.1_1.1.1w-0+deb11u1_amd64.deb' -o "$root/libssl.deb"
printf '%s  %s\n' aadf8b4b197335645b230c2839b4517aa444fd2e8f434e5438c48a18857988f7 "$root/libssl.deb" | sha256sum -c -
mkdir -p "$root/compat"
dpkg-deb -x "$root/libssl.deb" "$root/compat"
