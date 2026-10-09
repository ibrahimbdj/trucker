#!/bin/sh

#je gèrerais la config plus approfondis dès que je me chargerais des option
#pasta --quiet --no-netns-quit --config-net "$1"
pasta --quiet --config-net "$1"

if [ "$?" -eq 1 ]; then
    echo "pasta failed"
    exit 1
fi