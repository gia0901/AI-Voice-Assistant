#!/bin/bash

SSH_USER="gia"
SSH_HOST="192.168.7.2"
SSH_PRJ_BASE="/home/gia/bbb_voice_assistant"

print_menu() {
    echo "============= Copy-to-BBB ============="
    echo "1. Copy app"
    echo "2. Copy config folder"
    echo "3. Copy BBB-VOICE-ASSISTANT.dts"
    echo "99. Exit"
}

scp_to_target() {
    local src="$1"
    local dest_dir="$2"
    echo "==== command: scp "$flags" "$src" "${SSH_USER}@${SSH_HOST}:${dest_dir}/""
    scp "$src" "${SSH_USER}@${SSH_HOST}:${dest_dir}/"
}

copy_app() {
    local src="./build/app/app"
    local dest_dir="${SSH_PRJ_BASE}"
    scp_to_target "$src" "$dest_dir"
}

copy_config() {
    local src="common/config/config.json"
    local dest_dir="${SSH_PRJ_BASE}/config"
    scp_to_target "$src" "$dest_dir"

    local src="common/config/default_config.json"
    scp_to_target "$src" "$dest_dir"
}

copy_dts() {
    local src="kernel/overlays/BBB-VOICE-ASSISTANT.dts"
    local dest_dir="${SSH_PRJ_BASE}/dts"
    scp_to_target "$src" "$dest_dir"
}

while true; do
    print_menu
    read -rp "Enter your choice: " choice

    case "$choice" in
        1) copy_app ;;
        2) copy_config ;;
        3) copy_dts ;;
        99) echo "Exiting."; break ;;
        *) echo "Invalid choice. Press Enter to try again."; read -r _ ;;
    esac
done
