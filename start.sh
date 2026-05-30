#!/bin/bash
set -e

# Aller dans le dossier contenant ce script (le dossier racine du projet)
cd "$(dirname "$0")"

# Créer le dossier build si besoin
if [ ! -d build ]; then
  mkdir build
fi

# Aller dans build
cd build

# Générer les fichiers de build
cmake ..

# Compiler le projet
make

./CPPAudioMixer 