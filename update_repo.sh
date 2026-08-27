#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

DIST="stable"
COMP="main"
ARCH="amd64"
REPO_NAME="OpenSnitch-TDE"
ORIGIN="OpenSnitchTDE"
LABEL="OpenSnitch TDE Repository"
DESC="Official APT Repository for OpenSnitch TDE (Trinity Desktop Environment native port)"

POOL_DIR="pool/$COMP/o/opensnitch-tde"
DISTS_DIR="dists/$DIST/$COMP/binary-$ARCH"

mkdir -p "$POOL_DIR"
mkdir -p "$DISTS_DIR"

# Find latest .deb
DEB_FILE=$(ls -t opensnitch-tde_*_amd64.deb 2>/dev/null | head -n 1 || true)
if [ -z "$DEB_FILE" ]; then
    echo "No .deb file found in root, looking in build directory..."
    DEB_FILE=$(ls -t build/opensnitch-tde_*_amd64.deb 2>/dev/null | head -n 1 || true)
fi

if [ -n "$DEB_FILE" ] && [ -f "$DEB_FILE" ]; then
    echo "Copying $DEB_FILE to $POOL_DIR/..."
    cp -a "$DEB_FILE" "$POOL_DIR/"
fi

# Generate Packages & Packages.gz
echo "Generating Packages files..."
dpkg-scanpackages --multiversion pool/ > "$DISTS_DIR/Packages"
gzip -9c "$DISTS_DIR/Packages" > "$DISTS_DIR/Packages.gz"

# Generate Release file
echo "Generating Release file..."
RELEASE_FILE="dists/$DIST/Release"

calc_hashes() {
    local alg="$1"
    local cmd="$2"
    echo "${alg}:"
    find "dists/$DIST" -type f \( -name "Packages" -o -name "Packages.gz" -o -name "Release" \) | sort | while read -r f; do
        rel_path="${f#dists/$DIST/}"
        if [ "$rel_path" != "Release" ]; then
            size=$(stat -c%s "$f")
            hash=$($cmd "$f" | awk '{print $1}')
            printf " %s %16d %s\n" "$hash" "$size" "$rel_path"
        fi
    done
}

DATE_STR=$(date -Ru)

cat <<EOF > "$RELEASE_FILE"
Architectures: $ARCH
Codename: $DIST
Components: $COMP
Date: $DATE_STR
Description: $DESC
Label: $LABEL
Origin: $ORIGIN
Suite: $DIST
$(calc_hashes "MD5Sum" "md5sum")
$(calc_hashes "SHA1" "sha1sum")
$(calc_hashes "SHA256" "sha256sum")
$(calc_hashes "SHA512" "sha512sum")
EOF

echo "APT repository successfully updated in pool/ and dists/!"
