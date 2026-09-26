#!/bin/bash

# Install the Second Life Viewer. This script can install the viewer both
# system-wide and for an individual user.

VT102_STYLE_NORMAL='\E[0m'
VT102_COLOR_RED='\E[31m'

SCRIPTSRC=$(readlink -f "$0" || echo "$0")
RUN_PATH=$(dirname "${SCRIPTSRC}" || echo .)
tarball_path=${RUN_PATH}

function prompt()
{
    local prompt=$1
    local input

    echo -n "$prompt"

    while read -r input; do
        case $input in
            [Yy]* )
                return 1
                ;;
            [Nn]* )
                return 0
                ;;
            * )
                echo "Please enter yes or no."
                echo -n "$prompt"
        esac
    done
}

function die()
{
    warn "$1"
    exit 1
}

function warn()
{
    echo -n -e "$VT102_COLOR_RED"
    echo "$1"
    echo -n -e "$VT102_STYLE_NORMAL"
}

function homedir_install()
{
    warn "You are not running as a privileged user, so you will only be able"
    warn "to install Vayu in your home directory. If you"
    warn "would like to install Vayu system-wide, please run"
    warn "this script as the root user, or with the 'sudo' command."
    echo

    prompt "Proceed with the installation? [Y/N]: "
    if [[ $? == 0 ]]; then
	exit 0
    fi

    install_to_prefix "$HOME/.vayu-install"
    $HOME/.vayu-install/etc/refresh_desktop_app_entry.sh
    $HOME/.vayu-install/etc/register_secondlifeprotocol.sh
    configure_firewalld
}

function root_install()
{
    local default_prefix="/opt/vayu-install"

    echo -n "Enter the desired installation directory [${default_prefix}]: ";
    read -r
    if [[ "$REPLY" = "" ]] ; then
	local install_prefix=$default_prefix
    else
	local install_prefix=$REPLY
    fi

    install_to_prefix "$install_prefix"

    mkdir -p /usr/local/share/applications
    "${install_prefix}"/etc/refresh_desktop_app_entry.sh
    "${install_prefix}"/etc/register_secondlifeprotocol.sh
    configure_firewalld
}

function configure_firewalld()
{
    command -v firewall-cmd >/dev/null 2>&1 || return 0
    systemctl is-active --quiet firewalld 2>/dev/null || return 0

    if firewall-cmd --query-port=12035-13000/udp >/dev/null 2>&1; then
        return 0
    fi

    echo
    echo "Firewalld is active. Opening UDP ports 12035-13000 ensures smooth"
    echo "simulator packet delivery and prevents packet loss on strict firewalls."

    if [ "$UID" == "0" ]; then
        echo " - Configuring firewalld ports (12035-13000/udp)..."
        firewall-cmd --permanent --add-port=12035-13000/udp >/dev/null 2>&1 && firewall-cmd --reload >/dev/null 2>&1
    elif [ -t 0 ]; then
        prompt "Would you like to configure firewalld now using sudo? [Y/N]: "
        if [[ $? == 1 ]]; then
            echo " - Configuring firewalld ports via sudo (12035-13000/udp)..."
            sudo firewall-cmd --permanent --add-port=12035-13000/udp && sudo firewall-cmd --reload
        fi
    fi
}

function install_to_prefix()
{
    test -e "$1" && backup_previous_installation "$1"
    mkdir -p "$1" || die "Failed to create installation directory!"

    echo " - Installing to $1"

    cp -a "${tarball_path}"/* "$1/" || die "Failed to complete the installation!"
}

function backup_previous_installation()
{
    local backup_dir="$1".backup-$(date -I)
    if [ -e "$backup_dir" ]; then
        backup_dir="$1".backup-$(date +%Y-%m-%d-%H%M%S)
    fi
    echo " - Backing up previous installation to $backup_dir"

    mv "$1" "$backup_dir" || die "Failed to create backup of existing installation!"
}


if [ "$UID" == "0" ]; then
    root_install
else
    homedir_install
fi
