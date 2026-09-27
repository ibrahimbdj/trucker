#!/bin/bash

exec systemd-run --user --scope -p Delegate=yes trucker deliver