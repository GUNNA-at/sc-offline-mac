#!/bin/zsh
# release.sh: build the app and pack it as launcher/build/sc-offline-mac.app.zip, the file a release carries
set -e; D="$(cd "$(dirname "$0")" && pwd)"
"$D/build.sh" >/dev/null
cd "$D/build" && rm -f sc-offline-mac.app.zip && ditto -c -k --keepParent sc-offline-mac.app sc-offline-mac.app.zip
echo "packed $D/build/sc-offline-mac.app.zip ($(du -h sc-offline-mac.app.zip | cut -f1)), sha256 $(shasum -a 256 sc-offline-mac.app.zip | cut -d' ' -f1)"
