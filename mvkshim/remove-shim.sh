#!/bin/zsh
L=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64; cd "$L" || exit 1
[ -f libMoltenVK.real.dylib ] && mv -f libMoltenVK.real.dylib libMoltenVK.dylib && echo "shim removed; MoltenVK 1.4.2 restored as libMoltenVK.dylib"
