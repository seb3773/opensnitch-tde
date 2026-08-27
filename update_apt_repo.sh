#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$SCRIPT_DIR"
PAGES_DIR="/tmp/opensnitch_gh_pages_$$"

echo "=================================================="
echo " OpenSnitch TDE - APT Repository & GitHub Pages Sync"
echo "=================================================="

# Check tools
for cmd in git dpkg-scanpackages gzip apt-ftparchive; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "[Error] Required command '$cmd' not found."
        exit 1
    fi
done

# Ensure we have at least one deb package
DEB_FILES=($(ls -t "$REPO_DIR"/opensnitch-tde_*.deb 2>/dev/null || true))
if [ ${#DEB_FILES[@]} -eq 0 ]; then
    echo "[Info] No .deb package found in root. Building latest package..."
    "$REPO_DIR/build_deb.sh"
    DEB_FILES=($(ls -t "$REPO_DIR"/opensnitch-tde_*.deb 2>/dev/null || true))
fi

if [ ${#DEB_FILES[@]} -eq 0 ]; then
    echo "[Error] Failed to locate or build .deb package!"
    exit 1
fi

echo "Staging packages..."
rm -rf "$PAGES_DIR"
mkdir -p "$PAGES_DIR"

REMOTE_URL="$(cd "$REPO_DIR" && git remote get-url origin)"
REMOTE_PAGES=$(git ls-remote --heads origin gh-pages 2>/dev/null || true)
if [ -n "$REMOTE_PAGES" ]; then
    echo "Cloning existing gh-pages branch from remote..."
    git clone --single-branch --branch gh-pages "$REMOTE_URL" "$PAGES_DIR"
else
    echo "Initializing new gh-pages branch..."
    cd "$PAGES_DIR"
    git init
    git checkout -b gh-pages
    git remote add origin "$REMOTE_URL"
fi

cd "$PAGES_DIR"

# Create standard APT pool and dists structure
POOL_DIR="$PAGES_DIR/pool/main/o/opensnitch-tde"
DISTS_DIR="$PAGES_DIR/dists/stable/main/binary-amd64"
mkdir -p "$POOL_DIR"
mkdir -p "$DISTS_DIR"

# Copy all .deb packages from root
for deb in "${DEB_FILES[@]}"; do
    cp -u "$deb" "$POOL_DIR/" 2>/dev/null || cp -a "$deb" "$POOL_DIR/"
    echo "  Added: $(basename "$deb")"
done

# Also copy latest .qsi if present
QSI_FILES=($(ls -t "$REPO_DIR"/setup_opensnitch-tde_*.qsi 2>/dev/null || true))
if [ ${#QSI_FILES[@]} -gt 0 ]; then
    for qsi in "${QSI_FILES[@]}"; do
        cp -u "$qsi" "$PAGES_DIR/" 2>/dev/null || cp -a "$qsi" "$PAGES_DIR/"
        echo "  Added QSI: $(basename "$qsi")"
    done
fi

# Generate Packages & Packages.gz
echo "Generating Packages index..."
dpkg-scanpackages --multiversion pool/ /dev/null > "$DISTS_DIR/Packages"
gzip -9c "$DISTS_DIR/Packages" > "$DISTS_DIR/Packages.gz"

# Generate Release file
echo "Generating Release manifest..."
apt-ftparchive \
  -o APT::FTPArchive::Release::Origin="OpenSnitchTDE" \
  -o APT::FTPArchive::Release::Label="OpenSnitch TDE Repository" \
  -o APT::FTPArchive::Release::Suite="stable" \
  -o APT::FTPArchive::Release::Codename="stable" \
  -o APT::FTPArchive::Release::Architectures="amd64" \
  -o APT::FTPArchive::Release::Components="main" \
  -o APT::FTPArchive::Release::Description="Official APT Repository for OpenSnitch TDE (Trinity Desktop Environment native port)" \
  release "$PAGES_DIR/dists/stable" > "$PAGES_DIR/dists/stable/Release"

# Copy assets (logo, favicon, icons, screenshots, etc.)
if [ -f "$REPO_DIR/konqi_opensnitch.png" ]; then
    cp -a "$REPO_DIR/konqi_opensnitch.png" "$PAGES_DIR/"
fi
if [ -d "$REPO_DIR/icons" ]; then
    mkdir -p "$PAGES_DIR/icons"
    cp -a "$REPO_DIR/icons"/* "$PAGES_DIR/icons/" 2>/dev/null || true
fi
if [ -d "$REPO_DIR/screenshots" ]; then
    mkdir -p "$PAGES_DIR/screenshots"
    cp -a "$REPO_DIR/screenshots"/* "$PAGES_DIR/screenshots/" 2>/dev/null || true
fi

# Create .nojekyll to prevent GitHub Pages Jekyll processing
touch "$PAGES_DIR/.nojekyll"

# Find latest file names for HTML download buttons
LATEST_DEB_NAME=$(basename "${DEB_FILES[0]}")
LATEST_VERSION=$(echo "$LATEST_DEB_NAME" | sed -n 's/.*opensnitch-tde_\([^_]*\)_.*/\1/p')
if [ -z "$LATEST_VERSION" ]; then
    LATEST_VERSION="1.5.8"
fi

LATEST_QSI_NAME=""
if [ ${#QSI_FILES[@]} -gt 0 ]; then
    LATEST_QSI_NAME=$(basename "${QSI_FILES[0]}")
fi

cat << EOF > "$PAGES_DIR/index.html"
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>OpenSnitch TDE v${LATEST_VERSION} - APT Repository</title>
  <link rel="icon" type="image/png" href="icons/opensnitch_icon.png">
  <meta name="description" content="Official APT Repository and download portal for OpenSnitch TDE - Lightweight native C++/TQt3 port of the OpenSnitch application firewall UI for Trinity Desktop Environment (TDE).">
  <style>
    :root {
      --bg: #12141a;
      --card-bg: #1c1f2b;
      --card-hover: #222738;
      --accent: #3a86ff;
      --accent-grad: linear-gradient(135deg, #3a86ff, #00f2fe);
      --text: #e2e8f0;
      --text-muted: #94a3b8;
      --code-bg: #0f1117;
      --border: #2e364f;
      --radius: 12px;
      --radius-sm: 8px;
    }

    * {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
    }

    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      background-color: var(--bg);
      color: var(--text);
      line-height: 1.6;
      padding: 40px 20px;
    }

    .container {
      max-width: 840px;
      margin: 0 auto;
    }

    header {
      text-align: center;
      margin-bottom: 40px;
    }

    .logo {
      width: 120px;
      height: 120px;
      margin-bottom: 16px;
      filter: drop-shadow(0 8px 24px rgba(58, 134, 255, 0.45));
      object-fit: contain;
      transition: transform 0.3s cubic-bezier(0.34, 1.56, 0.64, 1);
    }

    .logo:hover {
      transform: scale(1.08) rotate(3deg);
    }

    .badge {
      display: inline-block;
      padding: 4px 14px;
      font-size: 0.85rem;
      font-weight: 600;
      color: #fff;
      background: var(--accent-grad);
      border-radius: 20px;
      margin-bottom: 12px;
      text-transform: uppercase;
      letter-spacing: 0.5px;
    }

    .version-pill {
      display: inline-block;
      font-size: 1.1rem;
      font-weight: 600;
      color: #38bdf8;
      background: rgba(56, 189, 248, 0.12);
      border: 1px solid rgba(56, 189, 248, 0.35);
      padding: 2px 12px;
      border-radius: 20px;
      vertical-align: middle;
      margin-left: 8px;
    }

    h1 {
      font-size: 2.4rem;
      font-weight: 700;
      margin-bottom: 8px;
      display: flex;
      align-items: center;
      justify-content: center;
    }

    p.lead {
      font-size: 1.1rem;
      color: var(--text-muted);
      max-width: 680px;
      margin: 0 auto;
    }

    .header-actions {
      display: flex;
      justify-content: center;
      margin-top: 18px;
    }

    .btn {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      padding: 9px 18px;
      border-radius: var(--radius-sm);
      font-size: 0.95rem;
      font-weight: 600;
      text-decoration: none;
      transition: all 0.2s ease;
      cursor: pointer;
      border: none;
    }

    .btn-secondary {
      background: var(--card-bg);
      color: var(--text);
      border: 1px solid var(--border);
    }

    .btn-secondary:hover {
      background: var(--card-hover);
      border-color: #38bdf8;
      transform: translateY(-2px);
    }

    .card {
      background: var(--card-bg);
      border: 1px solid var(--border);
      border-radius: var(--radius);
      padding: 24px;
      margin-bottom: 24px;
      box-shadow: 0 10px 30px rgba(0, 0, 0, 0.3);
    }

    h2 {
      font-size: 1.3rem;
      margin-bottom: 14px;
      display: flex;
      align-items: center;
      gap: 10px;
      color: #fff;
    }

    /* Terminal & Code snippet box */
    .code-container {
      position: relative;
      margin-top: 10px;
    }

    pre {
      background: var(--code-bg);
      border: 1px solid var(--border);
      border-radius: var(--radius-sm);
      padding: 16px;
      padding-right: 80px;
      overflow-x: auto;
      font-family: "Courier New", Courier, monospace;
      font-size: 0.92rem;
      color: #38bdf8;
      line-height: 1.6;
    }

    .copy-btn {
      position: absolute;
      top: 12px;
      right: 12px;
      background: rgba(255, 255, 255, 0.08);
      border: 1px solid rgba(255, 255, 255, 0.18);
      color: var(--text);
      padding: 5px 12px;
      border-radius: 6px;
      font-size: 0.8rem;
      cursor: pointer;
      transition: all 0.2s;
    }

    .copy-btn:hover {
      background: var(--accent);
      color: #fff;
      border-color: var(--accent);
    }

    /* Downloads Grid */
    .downloads-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(230px, 1fr));
      gap: 16px;
      margin-top: 16px;
    }

    .download-card {
      background: #141722;
      border: 1px solid var(--border);
      border-radius: var(--radius-sm);
      padding: 18px;
      display: flex;
      flex-direction: column;
      justify-content: space-between;
      transition: all 0.2s ease;
    }

    .download-card:hover {
      transform: translateY(-2px);
      border-color: #38bdf8;
      background: var(--card-hover);
    }

    .download-header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 8px;
    }

    .download-title {
      font-size: 1.05rem;
      font-weight: 700;
      color: #fff;
    }

    .download-tag {
      font-size: 0.72rem;
      font-weight: 600;
      padding: 2px 8px;
      border-radius: 12px;
      background: rgba(58, 134, 255, 0.15);
      color: #60a5fa;
      border: 1px solid rgba(58, 134, 255, 0.3);
    }

    .download-desc {
      font-size: 0.85rem;
      color: var(--text-muted);
      margin-bottom: 14px;
      flex-grow: 1;
    }

    .btn-download {
      background: #1e293b;
      color: #38bdf8;
      border: 1px solid #334155;
      padding: 8px 14px;
      border-radius: 6px;
      text-align: center;
      text-decoration: none;
      font-weight: 600;
      font-size: 0.9rem;
      transition: all 0.2s;
    }

    .btn-download:hover {
      background: var(--accent);
      color: #ffffff;
      border-color: var(--accent);
    }

    .features-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(240px, 1fr));
      gap: 16px;
      margin-top: 14px;
    }

    .feature-item {
      background: #141722;
      border: 1px solid var(--border);
      border-radius: var(--radius-sm);
      padding: 16px;
    }

    .feature-icon {
      font-size: 1.4rem;
      margin-bottom: 6px;
      display: inline-block;
    }

    .feature-title {
      font-size: 0.98rem;
      font-weight: 700;
      color: #ffffff;
      margin-bottom: 4px;
    }

    .feature-text {
      font-size: 0.85rem;
      color: var(--text-muted);
      line-height: 1.45;
    }

    /* Screenshots gallery */
    .screenshots-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
      gap: 12px;
      margin-top: 14px;
    }

    .screenshot-thumb {
      border-radius: var(--radius-sm);
      overflow: hidden;
      border: 1px solid var(--border);
      cursor: pointer;
      background: #141722;
      transition: transform 0.2s ease, border-color 0.2s ease;
      aspect-ratio: 16 / 10;
    }

    .screenshot-thumb:hover {
      transform: scale(1.03);
      border-color: #38bdf8;
    }

    .screenshot-thumb img {
      width: 100%;
      height: 100%;
      object-fit: cover;
      display: block;
    }

    /* Modal Lightbox */
    .modal {
      display: none;
      position: fixed;
      z-index: 1000;
      top: 0;
      left: 0;
      width: 100vw;
      height: 100vh;
      background: rgba(0, 0, 0, 0.88);
      backdrop-filter: blur(8px);
      align-items: center;
      justify-content: center;
      padding: 20px;
    }

    .modal.active {
      display: flex;
    }

    .modal img {
      max-width: 90vw;
      max-height: 85vh;
      border-radius: var(--radius-sm);
      box-shadow: 0 20px 60px rgba(0, 0, 0, 0.8);
      border: 1px solid var(--border);
    }

    .modal-close {
      position: absolute;
      top: 20px;
      right: 28px;
      color: #fff;
      font-size: 2rem;
      cursor: pointer;
      font-weight: bold;
    }

    footer {
      text-align: center;
      margin-top: 48px;
      padding-top: 24px;
      border-top: 1px solid var(--border);
      color: var(--text-muted);
      font-size: 0.95rem;
    }

    footer a {
      color: #38bdf8;
      text-decoration: none;
    }

    footer a:hover {
      text-decoration: underline;
    }

    .footer-links {
      margin-top: 8px;
      font-size: 0.88rem;
    }
  </style>
</head>
<body>

  <div class="container">

    <!-- Header -->
    <header>
      <img src="konqi_opensnitch.png" alt="OpenSnitch TDE Logo" class="logo">
      <br>
      <span class="badge">Official APT Repository</span>
      <h1>OpenSnitch TDE <span class="version-pill">v${LATEST_VERSION}</span></h1>
      <p class="lead">
        A lightweight, high-performance native C++/TQt3 graphical user interface port for the OpenSnitch application firewall on Trinity Desktop Environment (TDE).
      </p>
      <div class="header-actions">
        <a href="https://github.com/seb3773/opensnitch-tde" class="btn btn-secondary" target="_blank" rel="noopener">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor"><path d="M12 0C5.37 0 0 5.37 0 12c0 5.31 3.435 9.795 8.205 11.385.6.105.825-.255.825-.57 0-.285-.015-1.23-.015-2.235-3.015.555-3.795-.735-4.035-1.41-.135-.345-.72-1.41-1.23-1.695-.42-.225-1.02-.78-.015-.795.945-.015 1.62.87 1.845 1.23 1.08 1.815 2.805 1.305 3.495.99.105-.78.42-1.305.765-1.605-2.67-.3-5.46-1.335-5.46-5.925 0-1.305.465-2.385 1.23-3.225-.12-.3-.54-1.53.12-3.18 0 0 1.005-.315 3.3 1.23.96-.27 1.98-.405 3-.405s2.04.135 3 .405c2.295-1.56 3.3-1.23 3.3-1.23.66 1.65.24 2.88.12 3.18.765.84 1.23 1.905 1.23 3.225 0 4.605-2.805 5.625-5.475 5.925.435.375.81 1.095.81 2.22 0 1.605-.015 2.895-.015 3.3 0 .315.225.69.825.57A12.02 12.02 0 0024 12c0-6.63-5.37-12-12-12z"/></svg>
          View on GitHub
        </a>
      </div>
    </header>

    <!-- Method 1: APT Repository -->
    <div class="card">
      <h2>
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="#38bdf8" stroke-width="2"><path d="M4 17l6-6-6-6M12 19h8"/></svg>
        Method 1: Add the APT Repository (Recommended)
      </h2>
      <p style="color: var(--text-muted); font-size: 0.95rem;">
        Add the official repository to your system to receive regular automated updates via apt:
      </p>

      <div class="code-container">
        <pre id="aptCode">echo "deb [trusted=yes] https://seb3773.github.io/opensnitch-tde/ stable main" | sudo tee /etc/apt/sources.list.d/opensnitch-tde.list
sudo apt update
sudo apt install opensnitch-tde</pre>
        <button class="copy-btn" onclick="copyCode('aptCode', this)">Copy</button>
      </div>
      <p style="color: var(--text-muted); font-size: 0.85rem; margin-top: 12px;">
        Compatible with Q4OS, Debian, Devuan, Ubuntu, Linux Mint and all Debian-based distributions.
      </p>
      <p style="color: var(--text-muted); font-size: 0.85rem; margin-top: 8px;">
        * Note: This is a native UI client. It connects seamlessly to the OpenSnitch daemon service (<code>opensnitchd</code>).
      </p>
    </div>

    <!-- Method 2: Direct Packages -->
    <div class="card">
      <h2>
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="#38bdf8" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4M7 10l5 5 5-5M12 15V3"/></svg>
        Method 2: Direct Package Download (.deb / .qsi)
      </h2>
      <p style="color: var(--text-muted); font-size: 0.95rem;">
        Choose the package format best suited for your distribution:
      </p>

      <div class="downloads-grid">
        <div class="download-card">
          <div class="download-header">
            <span class="download-title">Debian / TDE (.deb)</span>
            <span class="download-tag">Recommended</span>
          </div>
          <p class="download-desc">Standard package for Trinity Desktop / Debian and compatible systems.</p>
          <a href="pool/main/o/opensnitch-tde/${LATEST_DEB_NAME}" class="btn-download">
            Download .deb
          </a>
        </div>

        <div class="download-card">
          <div class="download-header">
            <span class="download-title">Q4OS Installer (.qsi)</span>
            <span class="download-tag">Q4OS 1-Click</span>
          </div>
          <p class="download-desc">Graphical one-click installer designed specifically for Q4OS Trinity desktop.</p>
          <a href="${LATEST_QSI_NAME}" class="btn-download">
            Download .qsi
          </a>
        </div>
      </div>
      <p style="color: var(--text-muted); font-size: 0.85rem; margin-top: 16px;">
        * Note: The Q4OS installer (.qsi) automatically configures the APT repository during installation for future updates.
      </p>
      <p style="color: var(--text-muted); font-size: 0.85rem; margin-top: 8px;">
        Compatible with Q4OS, Debian, Devuan, Ubuntu, Linux Mint and all Debian-based distributions.
      </p>
    </div>

    <!-- Key Features -->
    <div class="card">
      <h2>
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="#38bdf8" stroke-width="2"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"/></svg>
        Key Capabilities &amp; Architecture
      </h2>

      <div class="features-grid">
        <div class="feature-item">
          <span class="feature-icon">🚀</span>
          <div class="feature-title">Native C++/TQt3 Architecture</div>
          <div class="feature-text">Instant startup with zero Python runtime latency, minimal memory usage, and direct native desktop integration.</div>
        </div>

        <div class="feature-item">
          <span class="feature-icon">🛡️</span>
          <div class="feature-title">Real-time Connection Alerts</div>
          <div class="feature-text">Interactive prompt dialogs for outgoing connections with full process inspection, user tracking, and instant rule creation.</div>
        </div>

        <div class="feature-item">
          <span class="feature-icon">⚡</span>
          <div class="feature-title">High-Speed SQLite Engine</div>
          <div class="feature-text">Shared in-memory database with batch transaction inserts for ultra-fast event logging, filtering, and search.</div>
        </div>

        <div class="feature-item">
          <span class="feature-icon">🔍</span>
          <div class="feature-title">Native Async Reverse DNS</div>
          <div class="feature-text">Custom background reverse DNS resolution engine keeping the user interface fluid and responsive at all times.</div>
        </div>

        <div class="feature-item">
          <span class="feature-icon">📊</span>
          <div class="feature-title">Live Network Statistics</div>
          <div class="feature-text">Real-time tabs for active applications, target domains, ports, users, rules, and daemon node status.</div>
        </div>

        <div class="feature-item">
          <span class="feature-icon">📦</span>
          <div class="feature-title">Embedded UI Assets</div>
          <div class="feature-text">All icons and visual assets are compiled directly into the binary, reducing disk I/O and maximizing responsiveness.</div>
        </div>
      </div>
    </div>

    <!-- Screenshots -->
    <div class="card">
      <h2>
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="#38bdf8" stroke-width="2"><rect x="3" y="3" width="18" height="18" rx="2" ry="2"/><circle cx="8.5" cy="8.5" r="1.5"/><polyline points="21 15 16 10 5 21"/></svg>
        Screenshots
      </h2>
      <div class="screenshots-grid">
        <div class="screenshot-thumb" onclick="openModal('screenshots/screenshot_1.jpg')">
          <img src="screenshots/screenshot_1.jpg" alt="OpenSnitch TDE - Main Events and Network Activity">
        </div>
        <div class="screenshot-thumb" onclick="openModal('screenshots/screenshot_2.jpg')">
          <img src="screenshots/screenshot_2.jpg" alt="OpenSnitch TDE - Rules and Filtering Engine">
        </div>
        <div class="screenshot-thumb" onclick="openModal('screenshots/screenshot_3.jpg')">
          <img src="screenshots/screenshot_3.jpg" alt="OpenSnitch TDE - Network Nodes and Host Activity">
        </div>
        <div class="screenshot-thumb" onclick="openModal('screenshots/screenshot_4.jpg')">
          <img src="screenshots/screenshot_4.jpg" alt="OpenSnitch TDE - Application Overview and Statistics">
        </div>
        <div class="screenshot-thumb" onclick="openModal('screenshots/screenshot_5.jpg')">
          <img src="screenshots/screenshot_5.jpg" alt="OpenSnitch TDE - Connection Prompt and Rule Creation">
        </div>
        <div class="screenshot-thumb" onclick="openModal('screenshots/screenshot_6.jpg')">
          <img src="screenshots/screenshot_6.jpg" alt="OpenSnitch TDE - Process Details and Inspection">
        </div>
        <div class="screenshot-thumb" onclick="openModal('screenshots/screenshot_7.jpg')">
          <img src="screenshots/screenshot_7.jpg" alt="OpenSnitch TDE - Configuration and Preferences">
        </div>
      </div>
    </div>

    <!-- Footer -->
    <footer>
      <p>Source Code &amp; Releases: <a href="https://github.com/seb3773/opensnitch-tde" target="_blank" rel="noopener">github.com/seb3773/opensnitch-tde</a></p>
      <p style="margin-top: 6px;">Developed with ❤️ for the Trinity Desktop Environment community.</p>
      <p class="footer-links">
        <a href="http://trinitydesktop.org/" target="_blank" rel="noopener">http://trinitydesktop.org/</a> &bull; 
        <a href="https://www.q4os.org/" target="_blank" rel="noopener">https://www.q4os.org/</a> &bull; 
        <a href="https://www.q4os.org/forum/index.php" target="_blank" rel="noopener">https://www.q4os.org/forum/index.php</a> &bull; 
        <a href="https://github.com/evilsocket/opensnitch" target="_blank" rel="noopener">OpenSnitch Upstream</a>
      </p>
    </footer>

  </div>

  <!-- Lightbox Modal -->
  <div id="imageModal" class="modal" onclick="closeModal()">
    <span class="modal-close">&times;</span>
    <img id="modalImg" src="" alt="Enlarged screenshot" onclick="event.stopPropagation()">
  </div>

  <script>
    function copyCode(id, btn) {
      const text = document.getElementById(id).innerText;
      navigator.clipboard.writeText(text).then(() => {
        const orig = btn.innerText;
        btn.innerText = "Copied!";
        setTimeout(() => btn.innerText = orig, 2000);
      });
    }

    function openModal(src) {
      document.getElementById('modalImg').src = src;
      document.getElementById('imageModal').classList.add('active');
    }

    function closeModal() {
      document.getElementById('imageModal').classList.remove('active');
    }

    document.addEventListener('keydown', (e) => {
      if (e.key === 'Escape') closeModal();
    });
  </script>
</body>
</html>
EOF

# Git commit and push to gh-pages
echo "Committing and pushing to gh-pages branch..."
git add -A
git commit -m "Update APT repository and redesign landing page: $(date +'%Y-%m-%d %H:%M:%S')" || echo "No changes to commit."
git push origin gh-pages

echo "Cleaning up temporary directory..."
rm -rf "$PAGES_DIR"

echo "=================================================="
echo " SUCCESS: APT repository updated on gh-pages!"
echo " URL: https://seb3773.github.io/opensnitch-tde/"
echo "=================================================="
